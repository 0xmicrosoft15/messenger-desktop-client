/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/choose_theme_controller.h"

#include "ui/rp_widget.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/message_bubble.h"
#include "ui/wrap/vertical_layout.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_cloud_themes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h" // boxTitle.
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace Ui {
namespace {

constexpr auto kDisableElement = "disable"_cs;

[[nodiscard]] QImage GeneratePreview(not_null<Ui::ChatTheme*> theme) {
	const auto &background = theme->background();
	const auto &colors = background.colors;
	const auto size = st::settingsThemePreviewSize;
	auto prepared = background.prepared;
	const auto paintPattern = [&](QPainter &p, bool inverted) {
		if (prepared.isNull()) {
			return;
		}
		const auto w = prepared.width();
		const auto h = prepared.height();
		const auto scaled = size.scaled(
			st::windowMinWidth / 2,
			st::windowMinHeight / 2,
			Qt::KeepAspectRatio);
		const auto use = (scaled.width() > w || scaled.height() > h)
			? scaled.scaled({ w, h }, Qt::KeepAspectRatio)
			: scaled;
		const auto good = QSize(
			std::max(use.width(), 1),
			std::max(use.height(), 1));
		auto small = prepared.copy(QRect(
			QPoint(
				(w - good.width()) / 2,
				(h - good.height()) / 2),
			good));
		if (inverted) {
			small = Ui::InvertPatternImage(std::move(small));
		}
		p.drawImage(
			QRect(QPoint(), size * style::DevicePixelRatio()),
			small);
	};
	const auto fullsize = size * style::DevicePixelRatio();
	auto result = background.waitingForNegativePattern()
		? QImage(
			fullsize,
			QImage::Format_ARGB32_Premultiplied)
		: Ui::GenerateBackgroundImage(
			fullsize,
			colors.empty() ? std::vector{ 1, QColor(0, 0, 0) } : colors,
			background.gradientRotation,
			background.patternOpacity,
			paintPattern);
	if (background.waitingForNegativePattern()) {
		result.fill(Qt::black);
	}
	{
		auto p = QPainter(&result);
		const auto sent = QRect(
			QPoint(
				(size.width()
					- st::settingsThemeBubbleSize.width()
					- st::settingsThemeBubblePosition.x()),
				st::settingsThemeBubblePosition.y()),
			st::settingsThemeBubbleSize);
		const auto received = QRect(
			st::settingsThemeBubblePosition.x(),
			sent.y() + sent.height() + st::settingsThemeBubbleSkip,
			sent.width(),
			sent.height());
		const auto radius = st::settingsThemeBubbleRadius;

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		if (const auto pattern = theme->bubblesBackgroundPattern()) {
			auto bubble = pattern->pixmap.toImage().scaled(
				sent.size() * style::DevicePixelRatio(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation
			).convertToFormat(QImage::Format_ARGB32_Premultiplied);
			const auto corners = Images::CornersMask(radius);
			Images::prepareRound(bubble, corners);
			p.drawImage(sent, bubble);
		} else {
			p.setBrush(theme->palette()->msgOutBg()->c);
			p.drawRoundedRect(sent, radius, radius);
		}
		p.setBrush(theme->palette()->msgInBg()->c);
		p.drawRoundedRect(received, radius, radius);
	}
	Images::prepareRound(result, ImageRoundRadius::Large);
	return result;
}

[[nodiscard]] QImage GenerateEmptyPreview() {
	auto result = QImage(
		st::settingsThemePreviewSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(st::settingsThemeNotSupportedBg->c);
	Images::prepareRound(result, ImageRoundRadius::Large);
	return result;
}

} // namespace

struct ChooseThemeController::Entry {
	uint64 id = 0;
	std::shared_ptr<Ui::ChatTheme> theme;
	std::shared_ptr<Data::DocumentMedia> media;
	QImage preview;
	EmojiPtr emoji = nullptr;
	QRect geometry;
	bool chosen = false;
};

ChooseThemeController::ChooseThemeController(
	not_null<RpWidget*> parent,
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer)
: _controller(window)
, _peer(peer)
, _wrap(std::make_unique<VerticalLayout>(parent))
, _topShadow(std::make_unique<PlainShadow>(parent))
, _content(_wrap->add(object_ptr<RpWidget>(_wrap.get())))
, _inner(CreateChild<RpWidget>(_content.get()))
, _dark(Window::Theme::IsThemeDarkValue()) {
	init(parent->sizeValue());
}

ChooseThemeController::~ChooseThemeController() {
	_controller->clearPeerThemeOverride(_peer);
}

void ChooseThemeController::init(rpl::producer<QSize> outer) {
	using namespace rpl::mappers;

	const auto themes = &_controller->session().data().cloudThemes();
	const auto &list = themes->chatThemes();
	if (!list.empty()) {
		fill(list);
	} else {
		themes->refreshChatThemes();
		themes->chatThemesUpdated(
		) | rpl::take(1) | rpl::start_with_next([=] {
			fill(themes->chatThemes());
		}, lifetime());
	}

	const auto skip = st::normalFont->spacew * 2;
	_wrap->insert(
		0,
		object_ptr<FlatLabel>(
			_wrap.get(),
			tr::lng_chat_theme_title(),
			st::boxTitle),
		style::margins{ skip * 2, skip, skip * 2, 0 });
	_wrap->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(_wrap.get()).fillRect(clip, st::windowBg);
	}, lifetime());

	initButtons();
	initList();

	std::move(
		outer
	) | rpl::start_with_next([=](QSize outer) {
		_wrap->resizeToWidth(outer.width());
		_wrap->move(0, outer.height() - _wrap->height());
		const auto line = st::lineWidth;
		_topShadow->setGeometry(0, _wrap->y() - line, outer.width(), line);
	}, lifetime());

	rpl::combine(
		_shouldBeShown.value(),
		_forceHidden.value(),
		_1 && !_2
	) | rpl::start_with_next([=](bool shown) {
		_wrap->setVisible(shown);
		_topShadow->setVisible(shown);
	}, lifetime());
}

void ChooseThemeController::initButtons() {
	const auto controls = _wrap->add(object_ptr<RpWidget>(_wrap.get()));
	const auto cancel = CreateChild<RoundButton>(
		controls,
		tr::lng_cancel(),
		st::defaultLightButton);
	const auto apply = CreateChild<RoundButton>(
		controls,
		tr::lng_chat_theme_apply(),
		st::defaultActiveButton);
	const auto skip = st::normalFont->spacew * 2;
	controls->resize(
		skip + cancel->width() + skip + apply->width() + skip,
		apply->height() + skip);
	rpl::combine(
		controls->widthValue(),
		cancel->widthValue(),
		apply->widthValue()
	) | rpl::start_with_next([=](
			int outer,
			int cancelWidth,
			int applyWidth) {
		const auto inner = skip + cancelWidth + skip + applyWidth + skip;
		const auto left = (outer - inner) / 2;
		cancel->moveToLeft(left, 0);
		apply->moveToRight(left, 0);
	}, controls->lifetime());

	const auto changed = [=] {
		if (_chosen.isEmpty()) {
			return false;
		}
		const auto now = Ui::Emoji::Find(_peer->themeEmoji());
		if (_chosen == kDisableElement.utf16()) {
			return !now;
		}
		for (const auto &entry : _entries) {
			if (entry.id && entry.emoji->text() == _chosen) {
				return (now != entry.emoji);
			}
		}
		return false;
	};
	cancel->setClickedCallback([=] { close(); });
	apply->setClickedCallback([=] {
		if (const auto chosen = findChosen()) {
			if (Ui::Emoji::Find(_peer->themeEmoji()) != chosen->emoji) {
				const auto now = chosen->id ? _chosen : QString();
				_peer->setThemeEmoji(now);
				if (chosen->theme) {
					// Remember while changes propagate through event loop.
					_controller->pushLastUsedChatTheme(chosen->theme);
				}
				const auto api = &_peer->session().api();
				api->request(MTPmessages_SetChatTheme(
					_peer->input,
					MTP_string(now)
				)).done([=](const MTPUpdates &result) {
					api->applyUpdates(result);
				}).send();
			}
		}
		_controller->toggleChooseChatTheme(_peer);
	});
}

void ChooseThemeController::paintEntry(QPainter &p, const Entry &entry) {
	const auto geometry = entry.geometry;
	p.drawImage(geometry, entry.preview);

	const auto size = Ui::Emoji::GetSizeLarge();
	const auto factor = style::DevicePixelRatio();
	const auto skip = st::normalFont->spacew * 2;
	Ui::Emoji::Draw(
		p,
		entry.emoji,
		size,
		(geometry.x()
			+ (geometry.width() - (size / factor)) / 2),
		(geometry.y() + geometry.height() - (size / factor) - skip));

	if (entry.chosen) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::activeLineFg->p;
		const auto width = st::defaultFlatInput.borderWidth;
		pen.setWidth(width);
		p.setPen(pen);
		const auto add = st::lineWidth + width;
		p.drawRoundedRect(
			entry.geometry.marginsAdded({ add, add, add, add }),
			st::roundRadiusLarge + add,
			st::roundRadiusLarge + add);
	}
}

void ChooseThemeController::initList() {
	_content->resize(
		_content->width(),
		4 * st::normalFont->spacew + st::settingsThemePreviewSize.height());
	_inner->setMouseTracking(true);

	_inner->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_inner.get());
		for (const auto &entry : _entries) {
			if (entry.preview.isNull() || !clip.intersects(entry.geometry)) {
				continue;
			}
			paintEntry(p, entry);
		}
	}, lifetime());
	const auto byPoint = [=](QPoint position) -> Entry* {
		for (auto &entry : _entries) {
			if (entry.geometry.contains(position)) {
				return &entry;
			}
		}
		return nullptr;
	};
	const auto chosenText = [=](const Entry *entry) {
		if (!entry) {
			return QString();
		} else if (entry->id) {
			return entry->emoji->text();
		} else {
			return kDisableElement.utf16();
		}
	};
	_inner->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			_inner->setCursor(byPoint(mouse->pos())
				? style::cur_pointer
				: style::cur_default);
		} else if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			_pressed = chosenText(byPoint(mouse->pos()));
		} else if (type == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(event.get());
			const auto entry = byPoint(mouse->pos());
			const auto chosen = chosenText(entry);
			if (entry && chosen == _pressed && chosen != _chosen) {
				clearCurrentBackgroundState();
				if (const auto was = findChosen()) {
					was->chosen = false;
				}
				_chosen = chosen;
				entry->chosen = true;
				if (entry->theme || !entry->id) {
					_controller->overridePeerTheme(_peer, entry->theme);
				}
				_inner->update();
			}
			_pressed = QString();
		}
	}, lifetime());

	_content->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(event.get());
			if (key->key() == Qt::Key_Escape) {
				close();
			}
		}
	}, lifetime());
}

void ChooseThemeController::close() {
	if (const auto chosen = findChosen()) {
		if (Ui::Emoji::Find(_peer->themeEmoji()) != chosen->emoji) {
			clearCurrentBackgroundState();
		}
	}
	_controller->toggleChooseChatTheme(_peer);
}

void ChooseThemeController::clearCurrentBackgroundState() {
	if (const auto entry = findChosen()) {
		if (entry->theme) {
			entry->theme->clearBackgroundState();
		}
	}
}

auto ChooseThemeController::findChosen() -> Entry* {
	if (_chosen.isEmpty()) {
		return nullptr;
	}
	for (auto &entry : _entries) {
		if (!entry.id && _chosen == kDisableElement.utf16()) {
			return &entry;
		} else if (_chosen == entry.emoji->text()) {
			return &entry;
		}
	}
	return nullptr;
}

auto ChooseThemeController::findChosen() const -> const Entry* {
	return const_cast<ChooseThemeController*>(this)->findChosen();
}

void ChooseThemeController::fill(
		const std::vector<Data::ChatTheme> &themes) {
	if (themes.empty()) {
		return;
	}
	const auto count = int(themes.size()) + 1;
	const auto single = st::settingsThemePreviewSize;
	const auto skip = st::normalFont->spacew * 2;
	const auto full = single.width() * count + skip * (count + 1);
	_inner->resize(full, skip + single.height() + skip);

	const auto initial = Ui::Emoji::Find(_peer->themeEmoji());

	_dark.value(
	) | rpl::start_with_next([=](bool dark) {
		clearCurrentBackgroundState();
		if (_chosen.isEmpty() && initial) {
			_chosen = initial->text();
		}

		_cachingLifetime.destroy();
		const auto old = base::take(_entries);
		auto x = skip;
		_entries.push_back({
			.preview = GenerateEmptyPreview(),
			.emoji = Ui::Emoji::Find(QString::fromUtf8("\xe2\x9d\x8c")),
			.geometry = QRect(QPoint(x, skip), single),
			.chosen = (_chosen == kDisableElement.utf16()),
		});
		Assert(_entries.front().emoji != nullptr);
		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			_entries.front().preview = GenerateEmptyPreview();
		}, _cachingLifetime);

		x += single.width() + skip;
		for (const auto &theme : themes) {
			const auto emoji = Ui::Emoji::Find(theme.emoticon);
			if (!emoji) {
				continue;
			}
			const auto &used = dark ? theme.dark : theme.light;
			const auto id = used.id;
			_entries.push_back({
				.id = id,
				.emoji = emoji,
				.geometry = QRect(QPoint(x, skip), single),
				.chosen = (_chosen == emoji->text()),
			});
			_controller->cachedChatThemeValue(
				used
			) | rpl::filter([=](const std::shared_ptr<ChatTheme> &data) {
				return data && (data->key() == id);
			}) | rpl::take(
				1
			) | rpl::start_with_next([=](std::shared_ptr<ChatTheme> &&data) {
				const auto id = data->key();
				const auto i = ranges::find(_entries, id, &Entry::id);
				if (i == end(_entries)) {
					return;
				}
				const auto theme = data.get();
				i->theme = std::move(data);
				i->preview = GeneratePreview(theme);
				if (_chosen == i->emoji->text()) {
					_controller->overridePeerTheme(_peer, i->theme);
				}
				_inner->update();

				if (!theme->background().isPattern
					|| !theme->background().prepared.isNull()) {
					return;
				}
				// Subscribe to pattern loading if needed.
				theme->repaintBackgroundRequests(
				) | rpl::filter([=] {
					const auto i = ranges::find(
						_entries,
						id,
						&Entry::id);
					return (i == end(_entries))
						|| !i->theme->background().prepared.isNull();
				}) | rpl::take(1) | rpl::start_with_next([=] {
					const auto i = ranges::find(
						_entries,
						id,
						&Entry::id);
					if (i == end(_entries)) {
						return;
					}
					i->preview = GeneratePreview(theme);
					_inner->update();
				}, _cachingLifetime);
			}, _cachingLifetime);
			x += single.width() + skip;
		}
	}, lifetime());
	_shouldBeShown = true;
}

bool ChooseThemeController::shouldBeShown() const {
	return _shouldBeShown.current();
}

rpl::producer<bool> ChooseThemeController::shouldBeShownValue() const {
	return _shouldBeShown.value();
}

int ChooseThemeController::height() const {
	return shouldBeShown() ? _wrap->height() : 0;
}

void ChooseThemeController::hide() {
	_forceHidden = true;
}

void ChooseThemeController::show() {
	_forceHidden = false;
}

void ChooseThemeController::raise() {
	_wrap->raise();
	_topShadow->raise();
}

void ChooseThemeController::setFocus() {
	_content->setFocus();
}

rpl::lifetime &ChooseThemeController::lifetime() {
	return _wrap->lifetime();
}

} // namespace Ui
