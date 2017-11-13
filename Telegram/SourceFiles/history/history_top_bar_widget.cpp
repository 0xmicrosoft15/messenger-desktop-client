/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history_top_bar_widget.h"

#include <rpl/combine.h>
#include <rpl/combine_previous.h>
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "info/info_memento.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "shortcuts.h"
#include "auth_session.h"
#include "lang/lang_keys.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "dialogs/dialogs_layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "calls/calls_instance.h"
#include "observer_peer.h"
#include "apiwrap.h"

HistoryTopBarWidget::HistoryTopBarWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _clearSelection(this, langFactory(lng_selected_clear), st::topBarClearButton)
, _forward(this, langFactory(lng_selected_forward), st::defaultActiveButton)
, _delete(this, langFactory(lng_selected_delete), st::defaultActiveButton)
, _call(this, st::topBarCall)
, _search(this, st::topBarSearch)
, _infoToggle(this, st::topBarInfo)
, _menuToggle(this, st::topBarMenuToggle)
, _onlineUpdater([this] { updateOnlineDisplay(); }) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	_forward->setClickedCallback([this] { onForwardSelection(); });
	_forward->setWidthChangedCallback([this] { updateControlsGeometry(); });
	_delete->setClickedCallback([this] { onDeleteSelection(); });
	_delete->setWidthChangedCallback([this] { updateControlsGeometry(); });
	_clearSelection->setClickedCallback([this] { onClearSelection(); });
	_call->setClickedCallback([this] { onCall(); });
	_search->setClickedCallback([this] { onSearch(); });
	_menuToggle->setClickedCallback([this] { showMenu(); });
	_infoToggle->setClickedCallback([this] { toggleInfoSection(); });

	rpl::combine(
		_controller->historyPeer.value(),
		_controller->searchInPeer.value())
		| rpl::combine_previous(std::make_tuple(nullptr, nullptr))
		| rpl::map([](
				const std::tuple<PeerData*, PeerData*> &previous,
				const std::tuple<PeerData*, PeerData*> &current) {
			auto peer = std::get<0>(current);
			auto searchPeer = std::get<1>(current);
			auto peerChanged = (peer != std::get<0>(previous));
			auto searchInPeer
				= (peer != nullptr) && (peer == searchPeer);
			return std::make_tuple(searchInPeer, peerChanged);
		})
		| rpl::start_with_next([this](
				bool searchInHistoryPeer,
				bool peerChanged) {
			auto animated = peerChanged
				? anim::type::instant
				: anim::type::normal;
			_search->setForceRippled(searchInHistoryPeer, animated);
		}, lifetime());

	subscribe(Adaptive::Changed(), [this]() { updateAdaptiveLayout(); });
	if (Adaptive::OneColumn()) {
		_unreadCounterSubscription = subscribe(Global::RefUnreadCounterUpdate(), [this] {
			rtlupdate(0, 0, st::titleUnreadCounterRight, st::titleUnreadCounterTop);
		});
	}
	subscribe(App::histories().sendActionAnimationUpdated(), [this](const Histories::SendActionAnimationUpdate &update) {
		if (update.history->peer == _historyPeer) {
			rtlupdate(0, 0, width(), height());
		}
	});
	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto flags = UpdateFlag::UserHasCalls
		| UpdateFlag::UserOnlineChanged
		| UpdateFlag::MembersChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(flags, [this](const Notify::PeerUpdate &update) {
		if (update.flags & UpdateFlag::UserHasCalls) {
			if (update.peer->isUser()) {
				updateControlsVisibility();
			}
		} else {
			updateOnlineDisplay();
		}
	}));
	subscribe(Global::RefPhoneCallsEnabledChanged(), [this] {
		updateControlsVisibility(); });

	rpl::combine(
		Auth().data().thirdSectionInfoEnabledValue(),
		Auth().data().tabbedReplacedWithInfoValue())
		| rpl::start_with_next(
			[this] { updateInfoToggleActive(); },
			lifetime());

	setCursor(style::cur_pointer);
	updateControlsVisibility();
}

void HistoryTopBarWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void HistoryTopBarWidget::onForwardSelection() {
	if (App::main()) App::main()->forwardSelectedItems();
}

void HistoryTopBarWidget::onDeleteSelection() {
	if (App::main()) App::main()->confirmDeleteSelectedItems();
}

void HistoryTopBarWidget::onClearSelection() {
	if (App::main()) App::main()->clearSelectedItems();
}

void HistoryTopBarWidget::onInfoClicked() {
	if (_historyPeer) {
		_controller->showPeerInfo(_historyPeer);
	}
}

void HistoryTopBarWidget::onSearch() {
	if (_historyPeer) {
		App::main()->searchInPeer(_historyPeer);
	}
}

void HistoryTopBarWidget::onCall() {
	if (auto user = _historyPeer->asUser()) {
		Calls::Current().startOutgoingCall(user);
	}
}

void HistoryTopBarWidget::showMenu() {
	if (!_historyPeer || _menu) {
		return;
	}
	_menu.create(parentWidget());
	_menu->setHiddenCallback([that = weak(this), menu = _menu.data()] {
		menu->deleteLater();
		if (that && that->_menu == menu) {
			that->_menu = nullptr;
			that->_menuToggle->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback(base::lambda_guarded(this, [this, menu = _menu.data()] {
		if (_menu == menu) {
			_menuToggle->setForceRippled(true);
		}
	}));
	_menu->setHideStartCallback(base::lambda_guarded(this, [this, menu = _menu.data()] {
		if (_menu == menu) {
			_menuToggle->setForceRippled(false);
		}
	}));
	_menuToggle->installEventFilter(_menu);
	Window::FillPeerMenu(
		_controller,
		_historyPeer,
		[this](const QString &text, base::lambda<void()> callback) {
			return _menu->addAction(text, std::move(callback));
		},
		Window::PeerMenuSource::History);
	_menu->moveToRight((parentWidget()->width() - width()) + st::topBarMenuPosition.x(), st::topBarMenuPosition.y());
	_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
}

void HistoryTopBarWidget::toggleInfoSection() {
	if (Adaptive::ThreeColumn()
		&& (Auth().data().thirdSectionInfoEnabled()
			|| Auth().data().tabbedReplacedWithInfo())) {
		_controller->closeThirdSection();
	} else if (_historyPeer) {
		if (_controller->canShowThirdSection()) {
			Auth().data().setThirdSectionInfoEnabled(true);
			Auth().saveDataDelayed();
			if (Adaptive::ThreeColumn()) {
				_controller->showSection(Info::Memento(_historyPeer->id));
			} else {
				_controller->resizeForThirdSection();
				_controller->updateColumnLayout();
			}
		} else {
			_controller->showSection(Info::Memento(_historyPeer->id));
		}
	} else {
		updateControlsVisibility();
	}
}

bool HistoryTopBarWidget::eventFilter(QObject *obj, QEvent *e) {
	if (obj == _membersShowArea) {
		switch (e->type()) {
		case QEvent::MouseButtonPress:
			mousePressEvent(static_cast<QMouseEvent*>(e));
			return true;

		case QEvent::Enter:
			_membersShowAreaActive.fire(true);
			break;

		case QEvent::Leave:
			_membersShowAreaActive.fire(false);
			break;
		}
	}
	return TWidget::eventFilter(obj, e);
}

void HistoryTopBarWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	_forward->stepNumbersAnimation(ms);
	_delete->stepNumbersAnimation(ms);
	auto hasSelected = (_selectedCount > 0);
	auto selectedButtonsTop = countSelectedButtonsTop(_selectedShown.current(getms(), hasSelected ? 1. : 0.));

	p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBg);
	if (selectedButtonsTop < 0) {
		p.translate(0, selectedButtonsTop + st::topBarHeight);

		p.save();
		auto decreaseWidth = 0;
		if (_info && !_info->isHidden()) {
			decreaseWidth += _info->width();
		}
		if (!_menuToggle->isHidden()) {
			decreaseWidth += _menuToggle->width();
		}
		if (!_infoToggle->isHidden()) {
			decreaseWidth += _infoToggle->width() + st::topBarSkip;
		}
		if (!_search->isHidden()) {
			decreaseWidth += _search->width();
		}
		if (!_call->isHidden()) {
			decreaseWidth += st::topBarCallSkip + _call->width();
		}
		paintTopBar(p, decreaseWidth, ms);
		p.restore();

		paintUnreadCounter(p, width(), _historyPeer);
	}
}

void HistoryTopBarWidget::paintTopBar(
		Painter &p,
		int decreaseWidth,
		TimeMs ms) {
	auto history = App::historyLoaded(_historyPeer);
	if (!history) return;

	auto increaseLeft = (Adaptive::OneColumn() || !App::main()->stackIsEmpty())
		? (st::topBarArrowPadding.left() - st::topBarArrowPadding.right())
		: 0;
	auto nameleft = st::topBarArrowPadding.right() + increaseLeft;
	auto nametop = st::topBarArrowPadding.top();
	auto statustop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	auto namewidth = width() - decreaseWidth - nameleft - st::topBarArrowPadding.right();
	p.setFont(st::dialogsTextFont);
	if (!history->paintSendAction(p, nameleft, statustop, namewidth, width(), st::historyStatusFgTyping, ms)) {
		p.setPen(_titlePeerTextOnline ? st::historyStatusFgActive : st::historyStatusFg);
		p.drawText(nameleft, statustop + st::dialogsTextFont->ascent, _titlePeerText);
	}

	p.setPen(st::dialogsNameFg);
	_historyPeer->dialogName().drawElided(p, nameleft, nametop, namewidth);

	if (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) {
		st::topBarBackward.paint(
			p,
			(st::topBarArrowPadding.left() - st::topBarBackward.width()) / 2,
			(st::topBarHeight - st::topBarBackward.height()) / 2,
			width());
	}
}

QRect HistoryTopBarWidget::getMembersShowAreaGeometry() const {
	int increaseLeft = (Adaptive::OneColumn() || !App::main()->stackIsEmpty())
		? (st::topBarArrowPadding.left() - st::topBarArrowPadding.right())
		: 0;
	int membersTextLeft = st::topBarArrowPadding.right() + increaseLeft;
	int membersTextTop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	int membersTextWidth = _titlePeerTextWidth;
	int membersTextHeight = st::topBarHeight - membersTextTop;

	return myrtlrect(membersTextLeft, membersTextTop, membersTextWidth, membersTextHeight);
}

void HistoryTopBarWidget::paintUnreadCounter(
		Painter &p,
		int outerWidth,
		PeerData *substractPeer) {
	if (!Adaptive::OneColumn()) {
		return;
	}
	auto mutedCount = App::histories().unreadMutedCount();
	auto fullCounter = App::histories().unreadBadge() + (Global::IncludeMuted() ? 0 : mutedCount);

	// Do not include currently shown chat in the top bar unread counter.
	if (auto historyShown = App::historyLoaded(substractPeer)) {
		auto shownUnreadCount = historyShown->unreadCount();
		if (!historyShown->mute() || Global::IncludeMuted()) {
			fullCounter -= shownUnreadCount;
		}
		if (historyShown->mute()) {
			mutedCount -= shownUnreadCount;
		}
	}

	if (auto counter = (fullCounter - (Global::IncludeMuted() ? 0 : mutedCount))) {
		auto counterText = (counter > 99) ? qsl("..%1").arg(counter % 100) : QString::number(counter);
		Dialogs::Layout::UnreadBadgeStyle unreadSt;
		unreadSt.muted = (mutedCount >= fullCounter);
		auto unreadRight = st::titleUnreadCounterRight;
		if (rtl()) unreadRight = outerWidth - st::titleUnreadCounterRight;
		auto unreadTop = st::titleUnreadCounterTop;
		Dialogs::Layout::paintUnreadCount(p, counterText, unreadRight, unreadTop, unreadSt);
	}
}

void HistoryTopBarWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton
		&& e->pos().y() < st::topBarHeight
		&& !_selectedCount) {
		clicked();
	}
}

void HistoryTopBarWidget::clicked() {
	if (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) {
		_controller->showBackFromStack();
	} else if (_historyPeer) {
		_controller->showPeerInfo(_historyPeer);
	}
}

void HistoryTopBarWidget::setHistoryPeer(
		not_null<PeerData*> historyPeer) {
	if (_historyPeer != historyPeer) {
		_historyPeer = historyPeer;
		if (_historyPeer) {
			_info.create(
				this,
				_controller,
				_historyPeer,
				Ui::UserpicButton::Role::OpenProfile,
				st::topBarInfoButton);
		} else {
			_info.destroy();
		}
		updateOnlineDisplay();
		updateControlsVisibility();
	}
}

void HistoryTopBarWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

int HistoryTopBarWidget::countSelectedButtonsTop(float64 selectedShown) {
	return (1. - selectedShown) * (-st::topBarHeight);
}

void HistoryTopBarWidget::updateControlsGeometry() {
	auto hasSelected = (_selectedCount > 0);
	auto selectedButtonsTop = countSelectedButtonsTop(_selectedShown.current(hasSelected ? 1. : 0.));
	auto otherButtonsTop = selectedButtonsTop + st::topBarHeight;
	auto buttonsLeft = st::topBarActionSkip + (Adaptive::OneColumn() ? 0 : st::lineWidth);
	auto buttonsWidth = _forward->contentWidth() + _delete->contentWidth() + _clearSelection->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 3;

	auto widthLeft = qMin(width() - buttonsWidth, -2 * st::defaultActiveButton.width);
	_forward->setFullWidth(-(widthLeft / 2));
	_delete->setFullWidth(-(widthLeft / 2));

	selectedButtonsTop += (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_forward->isHidden()) {
		buttonsLeft += _forward->width() + st::topBarActionSkip;
	}

	_delete->moveToLeft(buttonsLeft, selectedButtonsTop);
	_clearSelection->moveToRight(st::topBarActionSkip, selectedButtonsTop);

	auto right = 0;
	if (_info) {
		_info->moveToRight(right, otherButtonsTop);
	}
	_menuToggle->moveToRight(right, otherButtonsTop);
	if (!_info || _info->isHidden()) {
		right += _menuToggle->width() + st::topBarSkip;
	} else {
		right += _info->width();
	}
	_infoToggle->moveToRight(right, otherButtonsTop);
	if (!_infoToggle->isHidden()) {
		right += _infoToggle->width() + st::topBarSkip;
	}
	_search->moveToRight(right, otherButtonsTop);
	right += _search->width() + st::topBarCallSkip;
	_call->moveToRight(right, otherButtonsTop);
}

void HistoryTopBarWidget::animationFinished() {
	updateMembersShowArea();
	updateControlsVisibility();
}

void HistoryTopBarWidget::updateControlsVisibility() {
	_clearSelection->show();
	_delete->setVisible(_canDelete);
	_forward->setVisible(_canForward);

	if (Adaptive::OneColumn()
		|| (App::main() && !App::main()->stackIsEmpty())) {
		if (_info) {
			_info->show();
		}
		_menuToggle->hide();
		_menu.destroy();
	} else {
		if (_info) {
			_info->hide();
		}
		_menuToggle->show();
	}
	_search->show();
	_infoToggle->setVisible(!Adaptive::OneColumn()
		&& _controller->canShowThirdSection());
	auto callsEnabled = false;
	if (auto user = _historyPeer ? _historyPeer->asUser() : nullptr) {
		callsEnabled = Global::PhoneCallsEnabled() && user->hasCalls();
	}
	_call->setVisible(callsEnabled);

	if (_membersShowArea) {
		_membersShowArea->show();
	}
	updateControlsGeometry();
}

void HistoryTopBarWidget::updateMembersShowArea() {
	auto membersShowAreaNeeded = [this]() {
		auto peer = App::main()->peer();
		if ((_selectedCount > 0) || !peer) {
			return false;
		}
		if (auto chat = peer->asChat()) {
			return chat->amIn();
		}
		if (auto megagroup = peer->asMegagroup()) {
			return megagroup->canViewMembers() && (megagroup->membersCount() < Global::ChatSizeMax());
		}
		return false;
	};
	if (!membersShowAreaNeeded()) {
		if (_membersShowArea) {
			_membersShowAreaActive.fire(false);
			_membersShowArea.destroy();
		}
		return;
	} else if (!_membersShowArea) {
		_membersShowArea.create(this);
		_membersShowArea->show();
		_membersShowArea->installEventFilter(this);
	}
	_membersShowArea->setGeometry(getMembersShowAreaGeometry());
}

void HistoryTopBarWidget::showSelected(SelectedState state) {
	auto canDelete = (state.count > 0 && state.count == state.canDeleteCount);
	auto canForward = (state.count > 0 && state.count == state.canForwardCount);
	if (_selectedCount == state.count && _canDelete == canDelete && _canForward == canForward) {
		return;
	}
	if (state.count == 0) {
		// Don't change the visible buttons if the selection is cancelled.
		canDelete = _canDelete;
		canForward = _canForward;
	}

	auto wasSelected = (_selectedCount > 0);
	_selectedCount = state.count;
	if (_selectedCount > 0) {
		_forward->setNumbersText(_selectedCount);
		_delete->setNumbersText(_selectedCount);
		if (!wasSelected) {
			_forward->finishNumbersAnimation();
			_delete->finishNumbersAnimation();
		}
	}
	auto hasSelected = (_selectedCount > 0);
	if (_canDelete != canDelete || _canForward != canForward) {
		_canDelete = canDelete;
		_canForward = canForward;
		updateControlsVisibility();
	}
	if (wasSelected != hasSelected) {
		setCursor(hasSelected ? style::cur_default : style::cur_pointer);

		updateMembersShowArea();
		_selectedShown.start([this] { selectedShowCallback(); }, hasSelected ? 0. : 1., hasSelected ? 1. : 0., st::topBarSlideDuration, anim::easeOutCirc);
	} else {
		updateControlsGeometry();
	}
}

void HistoryTopBarWidget::selectedShowCallback() {
	updateControlsGeometry();
	update();
}

void HistoryTopBarWidget::updateAdaptiveLayout() {
	updateMembersShowArea();
	updateControlsVisibility();
	if (!Adaptive::OneColumn()) {
		unsubscribe(base::take(_unreadCounterSubscription));
	} else if (!_unreadCounterSubscription) {
		_unreadCounterSubscription = subscribe(Global::RefUnreadCounterUpdate(), [this] {
			rtlupdate(0, 0, st::titleUnreadCounterRight, st::titleUnreadCounterTop);
		});
	}
	updateInfoToggleActive();
}

void HistoryTopBarWidget::updateInfoToggleActive() {
	auto infoThirdActive = Adaptive::ThreeColumn()
		&& (Auth().data().thirdSectionInfoEnabled()
			|| Auth().data().tabbedReplacedWithInfo());
	auto iconOverride = infoThirdActive
		? &st::topBarInfoActive
		: nullptr;
	auto rippleOverride = infoThirdActive
		? &st::lightButtonBgOver
		: nullptr;
	_infoToggle->setIconOverride(iconOverride, iconOverride);
	_infoToggle->setRippleColorOverride(rippleOverride);
}

void HistoryTopBarWidget::updateOnlineDisplay() {
	if (!_historyPeer) return;

	QString text;
	int32 t = unixtime();
	bool titlePeerTextOnline = false;
	if (auto user = _historyPeer->asUser()) {
		text = App::onlineText(user, t);
		titlePeerTextOnline = App::onlineColorUse(user, t);
	} else if (auto chat = _historyPeer->asChat()) {
		if (!chat->amIn()) {
			text = lang(lng_chat_status_unaccessible);
		} else if (chat->participants.isEmpty()) {
			if (!_titlePeerText.isEmpty()) {
				text = _titlePeerText;
			} else if (chat->count <= 0) {
				text = lang(lng_group_status);
			} else {
				text = lng_chat_status_members(lt_count, chat->count);
			}
		} else {
			auto online = 0;
			auto onlyMe = true;
			for (auto i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
				if (i.key()->onlineTill > t) {
					++online;
					if (onlyMe && i.key() != App::self()) onlyMe = false;
				}
			}
			if (online > 0 && !onlyMe) {
				auto membersCount = lng_chat_status_members(lt_count, chat->participants.size());
				auto onlineCount = lng_chat_status_online(lt_count, online);
				text = lng_chat_status_members_online(lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (chat->participants.size() > 0) {
				text = lng_chat_status_members(lt_count, chat->participants.size());
			} else {
				text = lang(lng_group_status);
			}
		}
	} else if (auto channel = _historyPeer->asChannel()) {
		if (channel->isMegagroup() && channel->membersCount() > 0 && channel->membersCount() <= Global::ChatSizeMax()) {
			if (channel->mgInfo->lastParticipants.size() < channel->membersCount() || channel->lastParticipantsCountOutdated()) {
				Auth().api().requestLastParticipants(channel);
			}
			auto online = 0;
			bool onlyMe = true;
			for (auto &participant : std::as_const(channel->mgInfo->lastParticipants)) {
				if (participant->onlineTill > t) {
					++online;
					if (onlyMe && participant != App::self()) {
						onlyMe = false;
					}
				}
			}
			if (online && !onlyMe) {
				auto membersCount = lng_chat_status_members(lt_count, channel->membersCount());
				auto onlineCount = lng_chat_status_online(lt_count, online);
				text = lng_chat_status_members_online(lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (channel->membersCount() > 0) {
				text = lng_chat_status_members(lt_count, channel->membersCount());
			} else {
				text = lang(lng_group_status);
			}
		} else if (channel->membersCount() > 0) {
			text = lng_chat_status_members(lt_count, channel->membersCount());
		} else {
			text = lang(channel->isMegagroup() ? lng_group_status : lng_channel_status);
		}
	}
	if (_titlePeerText != text) {
		_titlePeerText = text;
		_titlePeerTextOnline = titlePeerTextOnline;
		_titlePeerTextWidth = st::dialogsTextFont->width(_titlePeerText);
		updateMembersShowArea();
		update();
	}
	updateOnlineDisplayTimer();
}

void HistoryTopBarWidget::updateOnlineDisplayTimer() {
	if (!_historyPeer) return;

	int32 t = unixtime(), minIn = 86400;
	if (auto user = _historyPeer->asUser()) {
		minIn = App::onlineWillChangeIn(user, t);
	} else if (auto chat = _historyPeer->asChat()) {
		if (chat->participants.isEmpty()) return;

		for (auto i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key(), t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else if (_historyPeer->isChannel()) {
	}
	updateOnlineDisplayIn(minIn * 1000);
}

void HistoryTopBarWidget::updateOnlineDisplayIn(TimeMs timeout) {
	_onlineUpdater.callOnce(timeout);
}
