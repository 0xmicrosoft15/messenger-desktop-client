/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_search_tags.h"

#include "base/qt/qt_key_modifiers.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "history/view/reactions/history_view_reactions.h"
#include "ui/effects/animation_value.h"
#include "ui/power_saving.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

[[nodiscard]] QString ComposeText(const Data::Reaction &tag) {
	auto result = tag.title;
	if (!result.isEmpty() && tag.count > 0) {
		result.append(' ');
	}
	if (tag.count > 0) {
		result.append(QString::number(tag.count));
	}
	return TextUtilities::SingleLine(result);
}

} // namespace

struct SearchTags::Tag {
	Data::ReactionId id;
	std::unique_ptr<Ui::Text::CustomEmoji> custom;
	QString text;
	int textWidth = 0;
	mutable QImage image;
	QRect geometry;
	ClickHandlerPtr link;
	bool selected = false;
};

SearchTags::SearchTags(
	not_null<Data::Session*> owner,
	rpl::producer<std::vector<Data::Reaction>> tags,
	std::vector<Data::ReactionId> selected)
: _owner(owner)
, _added(selected) {
	std::move(
		tags
	) | rpl::start_with_next([=](const std::vector<Data::Reaction> &list) {
		fill(list);
	}, _lifetime);

	// Mark the `selected` reactions as selected in `_tags`.
	for (const auto &id : selected) {
		const auto i = ranges::find(_tags, id, &Tag::id);
		if (i != end(_tags)) {
			i->selected = true;
		}
	}

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_normalBg = _selectedBg = QImage();
	}, _lifetime);
}

SearchTags::~SearchTags() = default;

void SearchTags::fill(const std::vector<Data::Reaction> &list) {
	const auto selected = collectSelected();
	_tags.clear();
	_tags.reserve(list.size());
	const auto link = [&](Data::ReactionId id) {
		return std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
			const auto i = ranges::find(_tags, id, &Tag::id);
			if (i != end(_tags)) {
				if (!i->selected && !base::IsShiftPressed()) {
					for (auto &tag : _tags) {
						tag.selected = false;
					}
				}
				i->selected = !i->selected;
				_selectedChanges.fire({});
			}
		}));
	};
	const auto push = [&](Data::ReactionId id, const QString &text) {
		const auto customId = id.custom();
		_tags.push_back({
			.id = id,
			.custom = (customId
				? _owner->customEmojiManager().create(
					customId,
					[=] { _repaintRequests.fire({}); })
				: nullptr),
			.text = text,
			.textWidth = st::reactionInlineTagFont->width(text),
			.link = link(id),
			.selected = ranges::contains(selected, id),
		});
		if (!customId) {
			_owner->reactions().preloadImageFor(id);
		}
	};
	for (const auto &reaction : list) {
		if (reaction.count > 0
			|| ranges::contains(_added, reaction.id)
			|| ranges::contains(selected, reaction.id)) {
			push(reaction.id, ComposeText(reaction));
		}
	}
	for (const auto &reaction : _added) {
		if (!ranges::contains(_tags, reaction, &Tag::id)) {
			push(reaction, QString());
		}
	}
	if (_width > 0) {
		layout();
		_repaintRequests.fire({});
	}
}

void SearchTags::layout() {
	Expects(_width > 0);

	if (_tags.empty()) {
		_height = 0;
		return;
	}
	const auto &bg = validateBg(false);
	const auto skip = st::dialogsSearchTagSkip;
	const auto size = bg.size() / bg.devicePixelRatio();
	const auto xbase = size.width();
	const auto ybase = size.height();
	auto x = 0;
	auto y = 0;
	for (auto &tag : _tags) {
		const auto width = xbase + tag.textWidth;
		if (x > 0 && x + width > _width) {
			x = 0;
			y += ybase + skip.y();
		}
		tag.geometry = QRect(x, y, width, xbase);
		x += width + skip.x();
	}
	_height = y + ybase + st::dialogsSearchTagBottom;
}

void SearchTags::resizeToWidth(int width) {
	if (_width == width || width <= 0) {
		return;
	}
	_width = width;
	layout();
}

int SearchTags::height() const {
	return _height.current();
}

rpl::producer<int> SearchTags::heightValue() const {
	return _height.value();
}

rpl::producer<> SearchTags::repaintRequests() const {
	return _repaintRequests.events();
}

ClickHandlerPtr SearchTags::lookupHandler(QPoint point) const {
	for (const auto &tag : _tags) {
		if (tag.geometry.contains(point.x(), point.y())) {
			return tag.link;
		}
	}
	return nullptr;
}

auto SearchTags::selectedValue() const
-> rpl::producer<std::vector<Data::ReactionId>> {
	return _selectedChanges.events() | rpl::map([=] {
		return collectSelected();
	});
}

void SearchTags::paintCustomFrame(
		QPainter &p,
		not_null<Ui::Text::CustomEmoji*> emoji,
		QPoint innerTopLeft,
		crl::time now,
		bool paused,
		const QColor &textColor) const {
	if (_customCache.isNull()) {
		using namespace Ui::Text;
		const auto size = st::emojiSize;
		const auto factor = style::DevicePixelRatio();
		const auto adjusted = AdjustCustomEmojiSize(size);
		_customCache = QImage(
			QSize(adjusted, adjusted) * factor,
			QImage::Format_ARGB32_Premultiplied);
		_customCache.setDevicePixelRatio(factor);
		_customSkip = (size - adjusted) / 2;
	}
	_customCache.fill(Qt::transparent);
	auto q = QPainter(&_customCache);
	emoji->paint(q, {
		.textColor = textColor,
		.now = now,
		.paused = paused || On(PowerSaving::kEmojiChat),
	});
	q.end();
	_customCache = Images::Round(
		std::move(_customCache),
		(Images::Option::RoundLarge
			| Images::Option::RoundSkipTopRight
			| Images::Option::RoundSkipBottomRight));

	p.drawImage(
		innerTopLeft + QPoint(_customSkip, _customSkip),
		_customCache);
}

void SearchTags::paint(
		QPainter &p,
		QPoint position,
		crl::time now,
		bool paused) const {
	const auto size = st::reactionInlineSize;
	const auto skip = (size - st::reactionInlineImage) / 2;
	const auto padding = st::reactionInlinePadding;
	for (const auto &tag : _tags) {
		const auto geometry = tag.geometry.translated(position);
		paintBackground(p, geometry, tag.selected);
		paintText(p, geometry, tag);
		if (!tag.custom && tag.image.isNull()) {
			tag.image = _owner->reactions().resolveImageFor(
				tag.id,
				::Data::Reactions::ImageSize::InlineList);
		}
		const auto inner = geometry.marginsRemoved(padding);
		const auto image = QRect(
			inner.topLeft() + QPoint(skip, skip),
			QSize(st::reactionInlineImage, st::reactionInlineImage));
		if (const auto custom = tag.custom.get()) {
			const auto textFg = tag.selected
				? st::dialogsNameFgActive->c
				: st::dialogsNameFgOver->c;
			paintCustomFrame(
				p,
				custom,
				inner.topLeft(),
				now,
				paused,
				textFg);
		} else if (!tag.image.isNull()) {
			p.drawImage(image.topLeft(), tag.image);
		}
	}
}

void SearchTags::paintBackground(
		QPainter &p,
		QRect geometry,
		bool selected) const {
	const auto &image = validateBg(selected);
	const auto ratio = int(image.devicePixelRatio());
	const auto size = image.size() / ratio;
	if (const auto fill = geometry.width() - size.width(); fill > 0) {
		const auto left = size.width() / 2;
		const auto right = size.width() - left;
		const auto x = geometry.x();
		const auto y = geometry.y();
		p.drawImage(
			QRect(x, y, left, size.height()),
			image,
			QRect(QPoint(), QSize(left, size.height()) * ratio));
		p.fillRect(
			QRect(x + left, y, fill, size.height()),
			bgColor(selected));
		p.drawImage(
			QRect(x + left + fill, y, right, size.height()),
			image,
			QRect(left * ratio, 0, right * ratio, size.height() * ratio));
	} else {
		p.drawImage(geometry.topLeft(), image);
	}
}

void SearchTags::paintText(QPainter &p, QRect geometry, const Tag &tag) const {
	using namespace HistoryView::Reactions;

	if (tag.text.isEmpty()) {
		return;
	}
	p.setPen(tag.selected ? st::dialogsTextFgActive : st::windowSubTextFg);
	p.setFont(st::reactionInlineTagFont);
	const auto x = geometry.x() + st::reactionInlineTagNamePosition.x();
	const auto y = geometry.y() + st::reactionInlineTagNamePosition.y();
	p.drawText(x, y + st::reactionInlineTagFont->ascent, tag.text);
}

QColor SearchTags::bgColor(bool selected) const {
	return selected
		? st::dialogsBgActive->c
		: st::dialogsBgOver->c;
}

const QImage &SearchTags::validateBg(bool selected) const {
	using namespace HistoryView::Reactions;
	auto &image = selected ? _selectedBg : _normalBg;
	if (image.isNull()) {
		const auto tagBg = bgColor(selected);
		const auto dotBg = st::transparent->c;
		image = InlineList::PrepareTagBg(tagBg, dotBg);
	}
	return image;
}

std::vector<Data::ReactionId> SearchTags::collectSelected() const {
	return _tags | ranges::views::filter(
		&Tag::selected
	) | ranges::views::transform(
		&Tag::id
	) | ranges::to_vector;
}

rpl::lifetime &SearchTags::lifetime() {
	return _lifetime;
}

} // namespace Dialogs
