/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/earn/info_earn_inner_widget.h"

#include "base/random.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_statistics.h"

namespace Info::ChannelEarn {
namespace {

void AddHeader(
		not_null<Ui::VerticalLayout*> content,
		tr::phrase<> text) {
	const auto header = content->add(
		object_ptr<Statistic::Header>(content),
		st::statisticsLayerMargins + st::boostsChartHeaderPadding);
	header->resizeToWidth(header->width());
	header->setTitle(text(tr::now));
	header->setSubTitle({});
}

void AddEmojiToMajor(
		not_null<Ui::FlatLabel*> label,
		not_null<Main::Session*> session,
		float64 value) {
	auto emoji = TextWithEntities{
		.text = (QString(QChar(0xD83D)) + QChar(0xDC8E)),
	};
	if (const auto e = Ui::Emoji::Find(emoji.text)) {
		const auto sticker = session->emojiStickersPack().stickerForEmoji(e);
		if (sticker.document) {
			emoji = Data::SingleCustomEmoji(sticker.document);
		}
	}
	label->setMarkedText(
		emoji.append(' ').append(QString::number(int64(value))),
		Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [label] { label->update(); },
		});
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: VerticalLayout(parent)
, _controller(controller)
, _peer(peer)
, _show(controller->uiShow()) {
}

void InnerWidget::load() {
}

void InnerWidget::fill() {
	const auto container = this;

	const auto session = &_peer->session();
	{
		const auto emoji = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				st::topicButtonArrow,
				st::channelEarnLearnArrowMargins,
				false));
		auto label = object_ptr<Ui::FlatLabel>(
			container,
			st::boxDividerLabel);
		const auto raw = label.data();
		tr::lng_channel_earn_about(
			lt_link,
			tr::lng_channel_earn_about_link(
				lt_emoji,
				rpl::single(emoji),
				Ui::Text::RichLangValue
			) | rpl::map([](TextWithEntities text) {
				return Ui::Text::Link(std::move(text), 1);
			}),
			Ui::Text::RichLangValue
		) | rpl::start_with_next([=](const TextWithEntities &text) {
			raw->setMarkedText(
				text,
				Core::MarkedTextContext{ .session = session });
		}, label->lifetime());
		label->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		}));
		container->add(object_ptr<Ui::DividerLabel>(
			container,
			std::move(label),
			st::defaultBoxDividerLabelPadding,
			RectPart::Top | RectPart::Bottom));
	}
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	{
		Ui::AddSkip(container);
		AddHeader(container, tr::lng_channel_earn_overview_title);
		Ui::AddSkip(container);
		Ui::AddSkip(container);

		const auto multiplier = 3.8; // Debug.
		const auto addOverviewEntry = [&](
				float64 value,
				const tr::phrase<> &text) {
			value = base::RandomIndex(1000000) / 1000.; // Debug.
			const auto line = container->add(
				Ui::CreateSkipWidget(container, 0),
				st::boxRowPadding);
			const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				st::channelEarnOverviewMajorLabel);
			AddEmojiToMajor(majorLabel, session, value);
			const auto minorLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				QString::number(value - int64(value)).mid(1),
				st::channelEarnOverviewMinorLabel);
			const auto secondMinorLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				QString(QChar(0x2248))
					+ QChar('$')
					+ QString::number(value * multiplier),
				st::channelEarnOverviewSubMinorLabel);
			rpl::combine(
				line->widthValue(),
				majorLabel->sizeValue()
			) | rpl::start_with_next([=](int available, const QSize &size) {
				line->resize(line->width(), size.height());
				minorLabel->moveToLeft(
					size.width(),
					st::channelEarnOverviewMinorLabelSkip);
				secondMinorLabel->resizeToWidth(available
					- size.width()
					- minorLabel->width());
				secondMinorLabel->moveToLeft(
					rect::right(minorLabel)
						+ st::channelEarnOverviewSubMinorLabelSkip,
					st::channelEarnOverviewSubMinorLabelSkip);
			}, minorLabel->lifetime());

			Ui::AddSkip(container);
			const auto sub = container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					text(),
					st::channelEarnOverviewSubMinorLabel),
				st::boxRowPadding);
			sub->setTextColorOverride(st::windowSubTextFg->c);
		};
		addOverviewEntry(0, tr::lng_channel_earn_available);
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		addOverviewEntry(0, tr::lng_channel_earn_reward);
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		addOverviewEntry(0, tr::lng_channel_earn_total);
		Ui::AddSkip(container);
	}
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	// memento->setState(base::take(_state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	// _state = memento->state();
	// if (!_state.link.isEmpty()) {
		fill();
	// } else {
	// 	load();
	// }
	Ui::RpWidget::resizeToWidth(width());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

auto InnerWidget::showRequests() const -> rpl::producer<ShowRequest> {
	return _showRequests.events();
}

void InnerWidget::showFinished() {
	_showFinished.fire({});
}

not_null<PeerData*> InnerWidget::peer() const {
	return _peer;
}

} // namespace Info::ChannelEarn

