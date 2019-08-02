/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/field_autocomplete.h"

#include "data/data_document.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "storage/localstorage.h"
#include "lottie/lottie_single_player.h"
#include "ui/widgets/scroll_area.h"
#include "ui/image/image.h"
#include "main/main_session.h"
#include "chat_helpers/stickers.h"
#include "base/unixtime.h"
#include "styles/style_history.h"
#include "styles/style_widgets.h"
#include "styles/style_chat_helpers.h"

FieldAutocomplete::FieldAutocomplete(
	QWidget *parent,
	not_null<Main::Session*> session)
: RpWidget(parent)
, _session(session)
, _scroll(this, st::mentionScroll) {
	_scroll->setGeometry(rect());

	_inner = _scroll->setOwnedWidget(
		object_ptr<internal::FieldAutocompleteInner>(
			this,
			&_mrows,
			&_hrows,
			&_brows,
			&_srows));
	_inner->setGeometry(rect());

	connect(_inner, SIGNAL(mentionChosen(UserData*, FieldAutocomplete::ChooseMethod)), this, SIGNAL(mentionChosen(UserData*, FieldAutocomplete::ChooseMethod)));
	connect(_inner, SIGNAL(hashtagChosen(QString, FieldAutocomplete::ChooseMethod)), this, SIGNAL(hashtagChosen(QString, FieldAutocomplete::ChooseMethod)));
	connect(_inner, SIGNAL(botCommandChosen(QString, FieldAutocomplete::ChooseMethod)), this, SIGNAL(botCommandChosen(QString, FieldAutocomplete::ChooseMethod)));
	connect(_inner, SIGNAL(stickerChosen(not_null<DocumentData*>,FieldAutocomplete::ChooseMethod)), this, SIGNAL(stickerChosen(not_null<DocumentData*>,FieldAutocomplete::ChooseMethod)));
	connect(_inner, SIGNAL(mustScrollTo(int, int)), _scroll, SLOT(scrollToY(int, int)));

	_scroll->show();
	_inner->show();

	hide();

	connect(_scroll, SIGNAL(geometryChanged()), _inner, SLOT(onParentGeometryChanged()));
}

FieldAutocomplete::~FieldAutocomplete() = default;

void FieldAutocomplete::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto opacity = _a_opacity.value(_hiding ? 0. : 1.);
	if (opacity < 1.) {
		if (opacity > 0.) {
			p.setOpacity(opacity);
			p.drawPixmap(0, 0, _cache);
		} else if (_hiding) {

		}
		return;
	}

	p.fillRect(rect(), st::mentionBg);
}

void FieldAutocomplete::showFiltered(
		not_null<PeerData*> peer,
		QString query,
		bool addInlineBots) {
	_chat = peer->asChat();
	_user = peer->asUser();
	_channel = peer->asChannel();
	if (query.isEmpty()) {
		_type = Type::Mentions;
		rowsUpdated(
			internal::MentionRows(),
			internal::HashtagRows(),
			internal::BotCommandRows(),
			base::take(_srows),
			false);
		return;
	}

	_emoji = nullptr;

	query = query.toLower();
	auto type = Type::Stickers;
	auto plainQuery = query.midRef(0);
	switch (query.at(0).unicode()) {
	case '@':
		type = Type::Mentions;
		plainQuery = query.midRef(1);
		break;
	case '#':
		type = Type::Hashtags;
		plainQuery = query.midRef(1);
		break;
	case '/':
		type = Type::BotCommands;
		plainQuery = query.midRef(1);
		break;
	}
	bool resetScroll = (_type != type || _filter != plainQuery);
	if (resetScroll) {
		_type = type;
		_filter = TextUtilities::RemoveAccents(plainQuery.toString());
	}
	_addInlineBots = addInlineBots;

	updateFiltered(resetScroll);
}

void FieldAutocomplete::showStickers(EmojiPtr emoji) {
	bool resetScroll = (_emoji != emoji);
	_emoji = emoji;
	_type = Type::Stickers;
	if (!emoji) {
		rowsUpdated(
			base::take(_mrows),
			base::take(_hrows),
			base::take(_brows),
			internal::StickerRows(),
			false);
		return;
	}

	_chat = nullptr;
	_user = nullptr;
	_channel = nullptr;

	updateFiltered(resetScroll);
}

bool FieldAutocomplete::clearFilteredBotCommands() {
	if (_brows.isEmpty()) return false;
	_brows.clear();
	return true;
}

namespace {
template <typename T, typename U>
inline int indexOfInFirstN(const T &v, const U &elem, int last) {
	for (auto b = v.cbegin(), i = b, e = b + qMax(v.size(), last); i != e; ++i) {
		if (*i == elem) {
			return (i - b);
		}
	}
	return -1;
}
}

internal::StickerRows FieldAutocomplete::getStickerSuggestions() {
	const auto list = Stickers::GetListByEmoji(
		_session,
		_emoji,
		_stickersSeed
	);
	auto result = ranges::view::all(
		list
	) | ranges::view::transform([](not_null<DocumentData*> sticker) {
		return internal::StickerSuggestion{ sticker };
	}) | ranges::to_vector;
	for (auto &suggestion : _srows) {
		if (!suggestion.animated) {
			continue;
		}
		const auto i = ranges::find(
			result,
			suggestion.document,
			&internal::StickerSuggestion::document);
		if (i != end(result)) {
			i->animated = std::move(suggestion.animated);
		}
	}
	return result;
}

void FieldAutocomplete::updateFiltered(bool resetScroll) {
	int32 now = base::unixtime::now(), recentInlineBots = 0;
	internal::MentionRows mrows;
	internal::HashtagRows hrows;
	internal::BotCommandRows brows;
	internal::StickerRows srows;
	if (_emoji) {
		srows = getStickerSuggestions();
	} else if (_type == Type::Mentions) {
		int maxListSize = _addInlineBots ? cRecentInlineBots().size() : 0;
		if (_chat) {
			maxListSize += (_chat->participants.empty() ? _chat->lastAuthors.size() : _chat->participants.size());
		} else if (_channel && _channel->isMegagroup()) {
			if (_channel->mgInfo->lastParticipants.empty() || _channel->lastParticipantsCountOutdated()) {
			} else {
				maxListSize += _channel->mgInfo->lastParticipants.size();
			}
		}
		if (maxListSize) {
			mrows.reserve(maxListSize);
		}

		auto filterNotPassedByUsername = [this](UserData *user) -> bool {
			if (user->username.startsWith(_filter, Qt::CaseInsensitive)) {
				bool exactUsername = (user->username.size() == _filter.size());
				return exactUsername;
			}
			return true;
		};
		auto filterNotPassedByName = [&](UserData *user) -> bool {
			for (const auto &nameWord : user->nameWords()) {
				if (nameWord.startsWith(_filter, Qt::CaseInsensitive)) {
					auto exactUsername = (user->username.compare(_filter, Qt::CaseInsensitive) == 0);
					return exactUsername;
				}
			}
			return filterNotPassedByUsername(user);
		};

		bool listAllSuggestions = _filter.isEmpty();
		if (_addInlineBots) {
			for_const (auto user, cRecentInlineBots()) {
				if (user->isInaccessible()) continue;
				if (!listAllSuggestions && filterNotPassedByUsername(user)) continue;
				mrows.push_back(user);
				++recentInlineBots;
			}
		}
		if (_chat) {
			auto ordered = QMultiMap<TimeId, not_null<UserData*>>();
			const auto byOnline = [&](not_null<UserData*> user) {
				return Data::SortByOnlineValue(user, now);
			};
			mrows.reserve(mrows.size() + (_chat->participants.empty() ? _chat->lastAuthors.size() : _chat->participants.size()));
			if (_chat->noParticipantInfo()) {
				Auth().api().requestFullPeer(_chat);
			} else if (!_chat->participants.empty()) {
				for (const auto user : _chat->participants) {
					if (user->isInaccessible()) continue;
					if (!listAllSuggestions && filterNotPassedByName(user)) continue;
					if (indexOfInFirstN(mrows, user, recentInlineBots) >= 0) continue;
					ordered.insertMulti(byOnline(user), user);
				}
			}
			for (const auto user : _chat->lastAuthors) {
				if (user->isInaccessible()) continue;
				if (!listAllSuggestions && filterNotPassedByName(user)) continue;
				if (indexOfInFirstN(mrows, user, recentInlineBots) >= 0) continue;
				mrows.push_back(user);
				if (!ordered.isEmpty()) {
					ordered.remove(byOnline(user), user);
				}
			}
			if (!ordered.isEmpty()) {
				for (auto i = ordered.cend(), b = ordered.cbegin(); i != b;) {
					--i;
					mrows.push_back(i.value());
				}
			}
		} else if (_channel && _channel->isMegagroup()) {
			QMultiMap<int32, UserData*> ordered;
			if (_channel->mgInfo->lastParticipants.empty() || _channel->lastParticipantsCountOutdated()) {
				Auth().api().requestLastParticipants(_channel);
			} else {
				mrows.reserve(mrows.size() + _channel->mgInfo->lastParticipants.size());
				for (const auto user : _channel->mgInfo->lastParticipants) {
					if (user->isInaccessible()) continue;
					if (!listAllSuggestions && filterNotPassedByName(user)) continue;
					if (indexOfInFirstN(mrows, user, recentInlineBots) >= 0) continue;
					mrows.push_back(user);
				}
			}
		}
	} else if (_type == Type::Hashtags) {
		bool listAllSuggestions = _filter.isEmpty();
		auto &recent(cRecentWriteHashtags());
		hrows.reserve(recent.size());
		for (const auto &item : recent) {
			const auto &tag = item.first;
			if (!listAllSuggestions
				&& (tag.size() == _filter.size()
					|| !TextUtilities::RemoveAccents(tag).startsWith(
						_filter,
						Qt::CaseInsensitive))) {
				continue;
			}
			hrows.push_back(tag);
		}
	} else if (_type == Type::BotCommands) {
		bool listAllSuggestions = _filter.isEmpty();
		bool hasUsername = _filter.indexOf('@') > 0;
		QMap<UserData*, bool> bots;
		int32 cnt = 0;
		if (_chat) {
			if (_chat->noParticipantInfo()) {
				_chat->session().api().requestFullPeer(_chat);
			} else if (!_chat->participants.empty()) {
				for (const auto user : _chat->participants) {
					if (!user->isBot()) {
						continue;
					} else if (!user->botInfo->inited) {
						user->session().api().requestFullPeer(user);
					}
					if (user->botInfo->commands.isEmpty()) {
						continue;
					}
					bots.insert(user, true);
					cnt += user->botInfo->commands.size();
				}
			}
		} else if (_user && _user->isBot()) {
			if (!_user->botInfo->inited) {
				_user->session().api().requestFullPeer(_user);
			}
			cnt = _user->botInfo->commands.size();
			bots.insert(_user, true);
		} else if (_channel && _channel->isMegagroup()) {
			if (_channel->mgInfo->bots.empty()) {
				if (!_channel->mgInfo->botStatus) {
					_channel->session().api().requestBots(_channel);
				}
			} else {
				for (const auto user : _channel->mgInfo->bots) {
					if (!user->isBot()) {
						continue;
					} else if (!user->botInfo->inited) {
						user->session().api().requestFullPeer(user);
					}
					if (user->botInfo->commands.isEmpty()) {
						continue;
					}
					bots.insert(user, true);
					cnt += user->botInfo->commands.size();
				}
			}
		}
		if (cnt) {
			brows.reserve(cnt);
			int32 botStatus = _chat ? _chat->botStatus : ((_channel && _channel->isMegagroup()) ? _channel->mgInfo->botStatus : -1);
			if (_chat) {
				for (const auto &user : _chat->lastAuthors) {
					if (!user->isBot()) {
						continue;
					} else if (!bots.contains(user)) {
						continue;
					} else if (!user->botInfo->inited) {
						user->session().api().requestFullPeer(user);
					}
					if (user->botInfo->commands.isEmpty()) {
						continue;
					}
					bots.remove(user);
					for (auto j = 0, l = user->botInfo->commands.size(); j != l; ++j) {
						if (!listAllSuggestions) {
							auto toFilter = (hasUsername || botStatus == 0 || botStatus == 2)
								? user->botInfo->commands.at(j).command + '@' + user->username
								: user->botInfo->commands.at(j).command;
							if (!toFilter.startsWith(_filter, Qt::CaseInsensitive)/* || toFilter.size() == _filter.size()*/) {
								continue;
							}
						}
						brows.push_back(qMakePair(user, &user->botInfo->commands.at(j)));
					}
				}
			}
			if (!bots.isEmpty()) {
				for (QMap<UserData*, bool>::const_iterator i = bots.cbegin(), e = bots.cend(); i != e; ++i) {
					UserData *user = i.key();
					for (int32 j = 0, l = user->botInfo->commands.size(); j < l; ++j) {
						if (!listAllSuggestions) {
							QString toFilter = (hasUsername || botStatus == 0 || botStatus == 2) ? user->botInfo->commands.at(j).command + '@' + user->username : user->botInfo->commands.at(j).command;
							if (!toFilter.startsWith(_filter, Qt::CaseInsensitive)/* || toFilter.size() == _filter.size()*/) continue;
						}
						brows.push_back(qMakePair(user, &user->botInfo->commands.at(j)));
					}
				}
			}
		}
	}
	rowsUpdated(
		std::move(mrows),
		std::move(hrows),
		std::move(brows),
		std::move(srows),
		resetScroll);
	_inner->setRecentInlineBotsInRows(recentInlineBots);
}

void FieldAutocomplete::rowsUpdated(
		internal::MentionRows &&mrows,
		internal::HashtagRows &&hrows,
		internal::BotCommandRows &&brows,
		internal::StickerRows &&srows,
		bool resetScroll) {
	if (mrows.isEmpty() && hrows.isEmpty() && brows.isEmpty() && srows.empty()) {
		if (!isHidden()) {
			hideAnimated();
		}
		_scroll->scrollToY(0);
		_mrows.clear();
		_hrows.clear();
		_brows.clear();
		_srows.clear();
	} else {
		_mrows = std::move(mrows);
		_hrows = std::move(hrows);
		_brows = std::move(brows);
		_srows = std::move(srows);

		bool hidden = _hiding || isHidden();
		if (hidden) {
			show();
			_scroll->show();
		}
		recount(resetScroll);
		update();
		if (hidden) {
			hide();
			showAnimated();
		}
	}
	_inner->rowsUpdated();
}

void FieldAutocomplete::setBoundings(QRect boundings) {
	_boundings = boundings;
	recount();
}

void FieldAutocomplete::recount(bool resetScroll) {
	int32 h = 0, oldst = _scroll->scrollTop(), st = oldst, maxh = 4.5 * st::mentionHeight;
	if (!_srows.empty()) {
		int32 stickersPerRow = qMax(1, int32(_boundings.width() - 2 * st::stickerPanPadding) / int32(st::stickerPanSize.width()));
		int32 rows = rowscount(_srows.size(), stickersPerRow);
		h = st::stickerPanPadding + rows * st::stickerPanSize.height();
	} else if (!_mrows.isEmpty()) {
		h = _mrows.size() * st::mentionHeight;
	} else if (!_hrows.isEmpty()) {
		h = _hrows.size() * st::mentionHeight;
	} else if (!_brows.isEmpty()) {
		h = _brows.size() * st::mentionHeight;
	}

	if (_inner->width() != _boundings.width() || _inner->height() != h) {
		_inner->resize(_boundings.width(), h);
	}
	if (h > _boundings.height()) h = _boundings.height();
	if (h > maxh) h = maxh;
	if (width() != _boundings.width() || height() != h) {
		setGeometry(_boundings.x(), _boundings.y() + _boundings.height() - h, _boundings.width(), h);
		_scroll->resize(_boundings.width(), h);
	} else if (y() != _boundings.y() + _boundings.height() - h) {
		move(_boundings.x(), _boundings.y() + _boundings.height() - h);
	}
	if (resetScroll) st = 0;
	if (st != oldst) _scroll->scrollToY(st);
	if (resetScroll) _inner->clearSel();
}

void FieldAutocomplete::hideFast() {
	_a_opacity.stop();
	hideFinish();
}

void FieldAutocomplete::hideAnimated() {
	if (isHidden() || _hiding) {
		return;
	}

	if (_cache.isNull()) {
		_scroll->show();
		_cache = Ui::GrabWidget(this);
	}
	_scroll->hide();
	_hiding = true;
	_a_opacity.start([this] { animationCallback(); }, 1., 0., st::emojiPanDuration);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void FieldAutocomplete::hideFinish() {
	hide();
	_hiding = false;
	_filter = qsl("-");
	_inner->clearSel(true);
}

void FieldAutocomplete::showAnimated() {
	if (!isHidden() && !_hiding) {
		return;
	}
	if (_cache.isNull()) {
		_stickersSeed = rand_value<uint64>();
		_scroll->show();
		_cache = Ui::GrabWidget(this);
	}
	_scroll->hide();
	_hiding = false;
	show();
	_a_opacity.start([this] { animationCallback(); }, 0., 1., st::emojiPanDuration);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void FieldAutocomplete::animationCallback() {
	update();
	if (!_a_opacity.animating()) {
		_cache = QPixmap();
		setAttribute(Qt::WA_OpaquePaintEvent);
		if (_hiding) {
			hideFinish();
		} else {
			_scroll->show();
			_inner->clearSel();
		}
	}
}

const QString &FieldAutocomplete::filter() const {
	return _filter;
}

ChatData *FieldAutocomplete::chat() const {
	return _chat;
}

ChannelData *FieldAutocomplete::channel() const {
	return _channel;
}

UserData *FieldAutocomplete::user() const {
	return _user;
}

int32 FieldAutocomplete::innerTop() {
	return _scroll->scrollTop();
}

int32 FieldAutocomplete::innerBottom() {
	return _scroll->scrollTop() + _scroll->height();
}

bool FieldAutocomplete::chooseSelected(ChooseMethod method) const {
	return _inner->chooseSelected(method);
}

bool FieldAutocomplete::eventFilter(QObject *obj, QEvent *e) {
	auto hidden = isHidden();
	auto moderate = Global::ModerateModeEnabled();
	if (hidden && !moderate) return QWidget::eventFilter(obj, e);

	if (e->type() == QEvent::KeyPress) {
		QKeyEvent *ev = static_cast<QKeyEvent*>(e);
		if (!(ev->modifiers() & (Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier))) {
			if (!hidden) {
				if (ev->key() == Qt::Key_Up || ev->key() == Qt::Key_Down || (!_srows.empty() && (ev->key() == Qt::Key_Left || ev->key() == Qt::Key_Right))) {
					return _inner->moveSel(ev->key());
				} else if (ev->key() == Qt::Key_Enter || ev->key() == Qt::Key_Return) {
					return _inner->chooseSelected(ChooseMethod::ByEnter);
				}
			}
			if (moderate && ((ev->key() >= Qt::Key_1 && ev->key() <= Qt::Key_9) || ev->key() == Qt::Key_Q)) {
				bool handled = false;
				emit moderateKeyActivate(ev->key(), &handled);
				return handled;
			}
		}
	}
	return QWidget::eventFilter(obj, e);
}

namespace internal {

FieldAutocompleteInner::FieldAutocompleteInner(
	not_null<FieldAutocomplete*> parent,
	not_null<MentionRows*> mrows,
	not_null<HashtagRows*> hrows,
	not_null<BotCommandRows*> brows,
	not_null<StickerRows*> srows)
: _parent(parent)
, _mrows(mrows)
, _hrows(hrows)
, _brows(brows)
, _srows(srows)
, _previewTimer([=] { showPreview(); }) {
	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
}

void FieldAutocompleteInner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect());
	if (r != rect()) p.setClipRect(r);

	auto atwidth = st::mentionFont->width('@');
	auto hashwidth = st::mentionFont->width('#');
	auto mentionleft = 2 * st::mentionPadding.left() + st::mentionPhotoSize;
	auto mentionwidth = width()
		- mentionleft
		- 2 * st::mentionPadding.right();
	auto htagleft = st::historyAttach.width
		+ st::historyComposeField.textMargins.left()
		- st::lineWidth;
	auto htagwidth = width()
		- st::mentionPadding.right()
		- htagleft
		- st::mentionScroll.width;

	if (!_srows->empty()) {
		int32 rows = rowscount(_srows->size(), _stickersPerRow);
		int32 fromrow = floorclamp(r.y() - st::stickerPanPadding, st::stickerPanSize.height(), 0, rows);
		int32 torow = ceilclamp(r.y() + r.height() - st::stickerPanPadding, st::stickerPanSize.height(), 0, rows);
		int32 fromcol = floorclamp(r.x() - st::stickerPanPadding, st::stickerPanSize.width(), 0, _stickersPerRow);
		int32 tocol = ceilclamp(r.x() + r.width() - st::stickerPanPadding, st::stickerPanSize.width(), 0, _stickersPerRow);
		for (int32 row = fromrow; row < torow; ++row) {
			for (int32 col = fromcol; col < tocol; ++col) {
				int32 index = row * _stickersPerRow + col;
				if (index >= _srows->size()) break;

				auto &sticker = (*_srows)[index];
				const auto document = sticker.document;
				if (!document->sticker()) continue;

				if (document->sticker()->animated
					&& !sticker.animated
					&& document->loaded()) {
					setupLottie(sticker);
				}

				QPoint pos(st::stickerPanPadding + col * st::stickerPanSize.width(), st::stickerPanPadding + row * st::stickerPanSize.height());
				if (_sel == index) {
					QPoint tl(pos);
					if (rtl()) tl.setX(width() - tl.x() - st::stickerPanSize.width());
					App::roundRect(p, QRect(tl, st::stickerPanSize), st::emojiPanHover, StickerHoverCorners);
				}

				document->checkStickerSmall();
				auto w = 1;
				auto h = 1;
				if (sticker.animated && !document->dimensions.isEmpty()) {
					const auto request = Lottie::FrameRequest{ stickerBoundingBox() * cIntRetinaFactor() };
					const auto size = request.size(document->dimensions) / cIntRetinaFactor();
					w = std::max(size.width(), 1);
					h = std::max(size.height(), 1);
				} else {
					const auto coef = std::min(
						std::min(
							(st::stickerPanSize.width() - st::buttonRadius * 2) / float64(document->dimensions.width()),
							(st::stickerPanSize.height() - st::buttonRadius * 2) / float64(document->dimensions.height())),
						1.);
					w = std::max(qRound(coef * document->dimensions.width()), 1);
					h = std::max(qRound(coef * document->dimensions.height()), 1);
				}
				if (sticker.animated && sticker.animated->ready()) {
					const auto frame = sticker.animated->frame();
					sticker.animated->markFrameShown();
					const auto size = frame.size() / cIntRetinaFactor();
					const auto ppos = pos + QPoint(
						(st::stickerPanSize.width() - size.width()) / 2,
						(st::stickerPanSize.height() - size.height()) / 2);
					p.drawImage(
						QRect(ppos, size),
						frame);
				} else if (const auto image = document->getStickerSmall()) {
					QPoint ppos = pos + QPoint((st::stickerPanSize.width() - w) / 2, (st::stickerPanSize.height() - h) / 2);
					p.drawPixmapLeft(ppos, width(), image->pix(document->stickerSetOrigin(), w, h));
				}
			}
		}
	} else {
		int32 from = qFloor(e->rect().top() / st::mentionHeight), to = qFloor(e->rect().bottom() / st::mentionHeight) + 1;
		int32 last = _mrows->isEmpty() ? (_hrows->isEmpty() ? _brows->size() : _hrows->size()) : _mrows->size();
		auto filter = _parent->filter();
		bool hasUsername = filter.indexOf('@') > 0;
		int filterSize = filter.size();
		bool filterIsEmpty = filter.isEmpty();
		for (int32 i = from; i < to; ++i) {
			if (i >= last) break;

			bool selected = (i == _sel);
			if (selected) {
				p.fillRect(0, i * st::mentionHeight, width(), st::mentionHeight, st::mentionBgOver);
				int skip = (st::mentionHeight - st::smallCloseIconOver.height()) / 2;
				if (!_hrows->isEmpty() || (!_mrows->isEmpty() && i < _recentInlineBotsInRows)) {
					st::smallCloseIconOver.paint(p, QPoint(width() - st::smallCloseIconOver.width() - skip, i * st::mentionHeight + skip), width());
				}
			}
			if (!_mrows->isEmpty()) {
				const auto user = _mrows->at(i);
				auto first = (!filterIsEmpty && user->username.startsWith(filter, Qt::CaseInsensitive)) ? ('@' + user->username.mid(0, filterSize)) : QString();
				auto second = first.isEmpty() ? (user->username.isEmpty() ? QString() : ('@' + user->username)) : user->username.mid(filterSize);
				auto firstwidth = st::mentionFont->width(first);
				auto secondwidth = st::mentionFont->width(second);
				auto unamewidth = firstwidth + secondwidth;
				auto namewidth = user->nameText().maxWidth();
				if (mentionwidth < unamewidth + namewidth) {
					namewidth = (mentionwidth * namewidth) / (namewidth + unamewidth);
					unamewidth = mentionwidth - namewidth;
					if (firstwidth < unamewidth + st::mentionFont->elidew) {
						if (firstwidth < unamewidth) {
							first = st::mentionFont->elided(first, unamewidth);
						} else if (!second.isEmpty()) {
							first = st::mentionFont->elided(first + second, unamewidth);
							second = QString();
						}
					} else {
						second = st::mentionFont->elided(second, unamewidth - firstwidth);
					}
				}
				user->loadUserpic();
				user->paintUserpicLeft(p, st::mentionPadding.left(), i * st::mentionHeight + st::mentionPadding.top(), width(), st::mentionPhotoSize);

				p.setPen(selected ? st::mentionNameFgOver : st::mentionNameFg);
				user->nameText().drawElided(p, 2 * st::mentionPadding.left() + st::mentionPhotoSize, i * st::mentionHeight + st::mentionTop, namewidth);

				p.setFont(st::mentionFont);
				p.setPen(selected ? st::mentionFgOverActive : st::mentionFgActive);
				p.drawText(mentionleft + namewidth + st::mentionPadding.right(), i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, first);
				if (!second.isEmpty()) {
					p.setPen(selected ? st::mentionFgOver : st::mentionFg);
					p.drawText(mentionleft + namewidth + st::mentionPadding.right() + firstwidth, i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, second);
				}
			} else if (!_hrows->isEmpty()) {
				QString hrow = _hrows->at(i);
				QString first = filterIsEmpty ? QString() : ('#' + hrow.mid(0, filterSize));
				QString second = filterIsEmpty ? ('#' + hrow) : hrow.mid(filterSize);
				int32 firstwidth = st::mentionFont->width(first), secondwidth = st::mentionFont->width(second);
				if (htagwidth < firstwidth + secondwidth) {
					if (htagwidth < firstwidth + st::mentionFont->elidew) {
						first = st::mentionFont->elided(first + second, htagwidth);
						second = QString();
					} else {
						second = st::mentionFont->elided(second, htagwidth - firstwidth);
					}
				}

				p.setFont(st::mentionFont);
				if (!first.isEmpty()) {
					p.setPen((selected ? st::mentionFgOverActive : st::mentionFgActive)->p);
					p.drawText(htagleft, i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, first);
				}
				if (!second.isEmpty()) {
					p.setPen((selected ? st::mentionFgOver : st::mentionFg)->p);
					p.drawText(htagleft + firstwidth, i * st::mentionHeight + st::mentionTop + st::mentionFont->ascent, second);
				}
			} else {
				UserData *user = _brows->at(i).first;

				const BotCommand *command = _brows->at(i).second;
				QString toHighlight = command->command;
				int32 botStatus = _parent->chat() ? _parent->chat()->botStatus : ((_parent->channel() && _parent->channel()->isMegagroup()) ? _parent->channel()->mgInfo->botStatus : -1);
				if (hasUsername || botStatus == 0 || botStatus == 2) {
					toHighlight += '@' + user->username;
				}
				user->loadUserpic();
				user->paintUserpicLeft(p, st::mentionPadding.left(), i * st::mentionHeight + st::mentionPadding.top(), width(), st::mentionPhotoSize);

				auto commandText = '/' + toHighlight;

				p.setPen(selected ? st::mentionNameFgOver : st::mentionNameFg);
				p.setFont(st::semiboldFont);
				p.drawText(2 * st::mentionPadding.left() + st::mentionPhotoSize, i * st::mentionHeight + st::mentionTop + st::semiboldFont->ascent, commandText);

				auto commandTextWidth = st::semiboldFont->width(commandText);
				auto addleft = commandTextWidth + st::mentionPadding.left();
				auto widthleft = mentionwidth - addleft;

				if (widthleft > st::mentionFont->elidew && !command->descriptionText().isEmpty()) {
					p.setPen((selected ? st::mentionFgOver : st::mentionFg)->p);
					command->descriptionText().drawElided(p, mentionleft + addleft, i * st::mentionHeight + st::mentionTop, widthleft);
				}
			}
		}
		p.fillRect(Adaptive::OneColumn() ? 0 : st::lineWidth, _parent->innerBottom() - st::lineWidth, width() - (Adaptive::OneColumn() ? 0 : st::lineWidth), st::lineWidth, st::shadowFg);
	}
	p.fillRect(Adaptive::OneColumn() ? 0 : st::lineWidth, _parent->innerTop(), width() - (Adaptive::OneColumn() ? 0 : st::lineWidth), st::lineWidth, st::shadowFg);
}

void FieldAutocompleteInner::resizeEvent(QResizeEvent *e) {
	_stickersPerRow = qMax(1, int32(width() - 2 * st::stickerPanPadding) / int32(st::stickerPanSize.width()));
}

void FieldAutocompleteInner::mouseMoveEvent(QMouseEvent *e) {
	const auto globalPosition = e->globalPos();
	if (!_lastMousePosition) {
		_lastMousePosition = globalPosition;
		return;
	} else if (!_mouseSelection
		&& *_lastMousePosition == globalPosition) {
		return;
	}
	selectByMouse(globalPosition);
}

void FieldAutocompleteInner::clearSel(bool hidden) {
	_overDelete = false;
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	setSel((_mrows->isEmpty() && _brows->isEmpty() && _hrows->isEmpty()) ? -1 : 0);
	if (hidden) {
		_down = -1;
		_previewShown = false;
	}
}

bool FieldAutocompleteInner::moveSel(int key) {
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;

	int32 maxSel = (_mrows->isEmpty() ? (_hrows->isEmpty() ? (_brows->isEmpty() ? _srows->size() : _brows->size()) : _hrows->size()) : _mrows->size());
	int32 direction = (key == Qt::Key_Up) ? -1 : (key == Qt::Key_Down ? 1 : 0);
	if (!_srows->empty()) {
		if (key == Qt::Key_Left) {
			direction = -1;
		} else if (key == Qt::Key_Right) {
			direction = 1;
		} else {
			direction *= _stickersPerRow;
		}
	}
	if (_sel >= maxSel || _sel < 0) {
		if (direction < -1) {
			setSel(((maxSel - 1) / _stickersPerRow) * _stickersPerRow, true);
		} else if (direction < 0) {
			setSel(maxSel - 1, true);
		} else {
			setSel(0, true);
		}
		return (_sel >= 0 && _sel < maxSel);
	}
	setSel((_sel + direction >= maxSel || _sel + direction < 0) ? -1 : (_sel + direction), true);
	return true;
}

bool FieldAutocompleteInner::chooseSelected(FieldAutocomplete::ChooseMethod method) const {
	if (!_srows->empty()) {
		if (_sel >= 0 && _sel < _srows->size()) {
			emit stickerChosen((*_srows)[_sel].document, method);
			return true;
		}
	} else if (!_mrows->isEmpty()) {
		if (_sel >= 0 && _sel < _mrows->size()) {
			emit mentionChosen(_mrows->at(_sel), method);
			return true;
		}
	} else if (!_hrows->isEmpty()) {
		if (_sel >= 0 && _sel < _hrows->size()) {
			emit hashtagChosen('#' + _hrows->at(_sel), method);
			return true;
		}
	} else if (!_brows->isEmpty()) {
		if (_sel >= 0 && _sel < _brows->size()) {
			UserData *user = _brows->at(_sel).first;
			const BotCommand *command(_brows->at(_sel).second);
			int32 botStatus = _parent->chat() ? _parent->chat()->botStatus : ((_parent->channel() && _parent->channel()->isMegagroup()) ? _parent->channel()->mgInfo->botStatus : -1);
			if (botStatus == 0 || botStatus == 2 || _parent->filter().indexOf('@') > 0) {
				emit botCommandChosen('/' + command->command + '@' + user->username, method);
			} else {
				emit botCommandChosen('/' + command->command, method);
			}
			return true;
		}
	}
	return false;
}

void FieldAutocompleteInner::setRecentInlineBotsInRows(int32 bots) {
	_recentInlineBotsInRows = bots;
}

void FieldAutocompleteInner::mousePressEvent(QMouseEvent *e) {
	selectByMouse(e->globalPos());
	if (e->button() == Qt::LeftButton) {
		if (_overDelete && _sel >= 0 && _sel < (_mrows->isEmpty() ? _hrows->size() : _recentInlineBotsInRows)) {
			bool removed = false;
			if (_mrows->isEmpty()) {
				QString toRemove = _hrows->at(_sel);
				RecentHashtagPack &recent(cRefRecentWriteHashtags());
				for (RecentHashtagPack::iterator i = recent.begin(); i != recent.cend();) {
					if (i->first == toRemove) {
						i = recent.erase(i);
						removed = true;
					} else {
						++i;
					}
				}
			} else {
				UserData *toRemove = _mrows->at(_sel);
				RecentInlineBots &recent(cRefRecentInlineBots());
				int32 index = recent.indexOf(toRemove);
				if (index >= 0) {
					recent.remove(index);
					removed = true;
				}
			}
			if (removed) {
				Local::writeRecentHashtagsAndBots();
			}
			_parent->updateFiltered();

			selectByMouse(e->globalPos());
		} else if (_srows->empty()) {
			chooseSelected(FieldAutocomplete::ChooseMethod::ByClick);
		} else {
			_down = _sel;
			_previewTimer.callOnce(QApplication::startDragTime());
		}
	}
}

void FieldAutocompleteInner::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.cancel();

	int32 pressed = _down;
	_down = -1;

	selectByMouse(e->globalPos());

	if (_previewShown) {
		_previewShown = false;
		return;
	}

	if (_sel < 0 || _sel != pressed || _srows->empty()) return;

	chooseSelected(FieldAutocomplete::ChooseMethod::ByClick);
}

void FieldAutocompleteInner::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

void FieldAutocompleteInner::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	if (_mouseSelection) {
		setSel(-1);
		_mouseSelection = false;
		_lastMousePosition = std::nullopt;
	}
}

void FieldAutocompleteInner::updateSelectedRow() {
	if (_sel >= 0) {
		if (_srows->empty()) {
			update(0, _sel * st::mentionHeight, width(), st::mentionHeight);
		} else {
			int32 row = _sel / _stickersPerRow, col = _sel % _stickersPerRow;
			update(st::stickerPanPadding + col * st::stickerPanSize.width(), st::stickerPanPadding + row * st::stickerPanSize.height(), st::stickerPanSize.width(), st::stickerPanSize.height());
		}
	}
}

void FieldAutocompleteInner::setSel(int sel, bool scroll) {
	updateSelectedRow();
	_sel = sel;
	updateSelectedRow();

	if (scroll && _sel >= 0) {
		if (_srows->empty()) {
			emit mustScrollTo(_sel * st::mentionHeight, (_sel + 1) * st::mentionHeight);
		} else {
			int32 row = _sel / _stickersPerRow;
			emit mustScrollTo(st::stickerPanPadding + row * st::stickerPanSize.height(), st::stickerPanPadding + (row + 1) * st::stickerPanSize.height());
		}
	}
}

void FieldAutocompleteInner::rowsUpdated() {
	if (_srows->empty()) {
		_stickersLifetime.destroy();
	}
}

auto FieldAutocompleteInner::getLottieRenderer()
-> std::shared_ptr<Lottie::FrameRenderer> {
	if (auto result = _lottieRenderer.lock()) {
		return result;
	}
	auto result = Lottie::MakeFrameRenderer();
	_lottieRenderer = result;
	return result;
}

void FieldAutocompleteInner::setupLottie(StickerSuggestion &suggestion) {
	const auto document = suggestion.document;
	suggestion.animated = Stickers::LottiePlayerFromDocument(
		document,
		Stickers::LottieSize::InlineResults,
		stickerBoundingBox() * cIntRetinaFactor(),
		Lottie::Quality::Default,
		getLottieRenderer());

	suggestion.animated->updates(
	) | rpl::start_with_next([=] {
		repaintSticker(document);
	}, _stickersLifetime);
}

QSize FieldAutocompleteInner::stickerBoundingBox() const {
	return QSize(
		st::stickerPanSize.width() - st::buttonRadius * 2,
		st::stickerPanSize.height() - st::buttonRadius * 2);
}

void FieldAutocompleteInner::repaintSticker(
		not_null<DocumentData*> document) {
	const auto i = ranges::find(
		*_srows,
		document,
		&StickerSuggestion::document);
	if (i == end(*_srows)) {
		return;
	}
	const auto index = (i - begin(*_srows));
	const auto row = (index / _stickersPerRow);
	const auto col = (index % _stickersPerRow);
	update(
		st::stickerPanPadding + col * st::stickerPanSize.width(),
		st::stickerPanPadding + row * st::stickerPanSize.height(),
		st::stickerPanSize.width(),
		st::stickerPanSize.height());
}

void FieldAutocompleteInner::selectByMouse(QPoint globalPosition) {
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	const auto mouse = mapFromGlobal(globalPosition);

	if (_down >= 0 && !_previewShown) {
		return;
	}

	int32 sel = -1, maxSel = 0;
	if (!_srows->empty()) {
		int32 rows = rowscount(_srows->size(), _stickersPerRow);
		int32 row = (mouse.y() >= st::stickerPanPadding) ? ((mouse.y() - st::stickerPanPadding) / st::stickerPanSize.height()) : -1;
		int32 col = (mouse.x() >= st::stickerPanPadding) ? ((mouse.x() - st::stickerPanPadding) / st::stickerPanSize.width()) : -1;
		if (row >= 0 && col >= 0) {
			sel = row * _stickersPerRow + col;
		}
		maxSel = _srows->size();
		_overDelete = false;
	} else {
		sel = mouse.y() / int32(st::mentionHeight);
		maxSel = _mrows->isEmpty() ? (_hrows->isEmpty() ? _brows->size() : _hrows->size()) : _mrows->size();
		_overDelete = (!_hrows->isEmpty() || (!_mrows->isEmpty() && sel < _recentInlineBotsInRows)) ? (mouse.x() >= width() - st::mentionHeight) : false;
	}
	if (sel < 0 || sel >= maxSel) {
		sel = -1;
	}
	if (sel != _sel) {
		setSel(sel);
		if (_down >= 0 && _sel >= 0 && _down != _sel) {
			_down = _sel;
			if (_down >= 0 && _down < _srows->size()) {
				if (const auto w = App::wnd()) {
					w->showMediaPreview(
						(*_srows)[_down].document->stickerSetOrigin(),
						(*_srows)[_down].document);
				}
			}
		}
	}
}

void FieldAutocompleteInner::onParentGeometryChanged() {
	const auto globalPosition = QCursor::pos();
	if (rect().contains(mapFromGlobal(globalPosition))) {
		setMouseTracking(true);
		if (_mouseSelection) {
			selectByMouse(globalPosition);
		}
	}
}

void FieldAutocompleteInner::showPreview() {
	if (_down >= 0 && _down < _srows->size()) {
		if (const auto w = App::wnd()) {
			w->showMediaPreview(
				(*_srows)[_down].document->stickerSetOrigin(),
				(*_srows)[_down].document);
			_previewShown = true;
		}
	}
}

} // namespace internal
