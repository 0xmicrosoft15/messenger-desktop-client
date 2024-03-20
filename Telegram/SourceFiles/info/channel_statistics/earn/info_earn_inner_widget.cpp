/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/earn/info_earn_inner_widget.h"

#include "base/random.h"
#include "base/unixtime.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h" // Info::Profile::NameValue.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_statistics.h"

#include <QUuid>
#include <QtWidgets/QApplication>

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

[[nodiscard]] QString FormatDate(TimeId date) {
	const auto parsedDate = base::unixtime::parse(date);
	return tr::lng_group_call_starts_short_date(
		tr::now,
		lt_date,
		langDayOfMonth(parsedDate.date()),
		lt_time,
		QLocale().toString(parsedDate.time(), QLocale::ShortFormat));
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

	constexpr auto kMinus = QChar(0x2212);
	const auto currency = u"TON"_q;

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
						+ st::channelEarnOverviewSubMinorLabelPos.x(),
					st::channelEarnOverviewSubMinorLabelPos.y());
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
	{
		Ui::AddSkip(container);
		AddHeader(container, tr::lng_channel_earn_history_title);
		Ui::AddSkip(container);
		Ui::AddSkip(container);

		struct HistoryEntry final {
			TimeId from = 0;
			TimeId to = 0;
			float64 value = 0;
			QString recipient;
			bool in = false;
		};

		const auto addHistoryEntry = [&](
				const HistoryEntry &entry,
				const tr::phrase<> &text) {
			const auto wrap = container->add(
				object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
					container,
					object_ptr<Ui::VerticalLayout>(container),
					QMargins()));
			const auto inner = wrap->entity();
			inner->setAttribute(Qt::WA_TransparentForMouseEvents);
			inner->add(object_ptr<Ui::FlatLabel>(
				inner,
				text(),
				st::channelEarnHistoryLabel));

			const auto recipient = Ui::Text::Wrapped(
				{ entry.recipient },
				EntityType::Code);
			if (!entry.recipient.isEmpty()) {
				Ui::AddSkip(inner, st::channelEarnHistoryThreeSkip);
				const auto label = inner->add(object_ptr<Ui::FlatLabel>(
					inner,
					rpl::single(recipient),
					st::channelEarnHistoryRecipientLabel));
				label->setBreakEverywhere(true);
				label->setTryMakeSimilarLines(true);
				Ui::AddSkip(inner, st::channelEarnHistoryThreeSkip);
			} else {
				Ui::AddSkip(inner, st::channelEarnHistoryTwoSkip);
			}

			const auto dateText = entry.to
				? (FormatDate(entry.from)
					+ ' '
					+ QChar(8212)
					+ ' '
					+ FormatDate(entry.to))
				: FormatDate(entry.from);
			inner->add(object_ptr<Ui::FlatLabel>(
				inner,
				dateText,
				st::channelEarnHistorySubLabel));

			const auto color = (entry.in
				? st::boxTextFgGood
				: st::menuIconAttentionColor)->c;
			const auto majorText = (entry.in ? '+' : kMinus)
				+ QString::number(int64(entry.value));
			const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
				wrap,
				majorText,
				st::channelEarnHistoryMajorLabel);
			majorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
			majorLabel->setTextColorOverride(color);
			const auto minorText =
				QString::number(entry.value - int64(entry.value)).mid(1)
					+ ' '
					+ currency;
			const auto minorLabel = Ui::CreateChild<Ui::FlatLabel>(
				wrap,
				minorText,
				st::channelEarnHistoryMinorLabel);
			minorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
			minorLabel->setTextColorOverride(color);
			const auto button = Ui::CreateChild<Ui::SettingsButton>(
				wrap,
				rpl::single(QString()));

			const auto detailsBox = [=, peer = _peer](
					not_null<Ui::GenericBox*> box) {
				Ui::AddSkip(box->verticalLayout());
				Ui::AddSkip(box->verticalLayout());
				const auto labels = box->addRow(
					object_ptr<Ui::CenterWrap<Ui::RpWidget>>(
						box,
						object_ptr<Ui::RpWidget>(box)))->entity();

				const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
					labels,
					majorText,
					st::channelEarnOverviewMajorLabel);
				majorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
				majorLabel->setTextColorOverride(color);
				const auto minorLabel = Ui::CreateChild<Ui::FlatLabel>(
					labels,
					minorText,
					st::channelEarnOverviewMinorLabel);
				minorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
				minorLabel->setTextColorOverride(color);
				rpl::combine(
					majorLabel->sizeValue(),
					minorLabel->sizeValue()
				) | rpl::start_with_next([=](
						const QSize &majorSize,
						const QSize &minorSize) {
					labels->resize(
						majorSize.width() + minorSize.width(),
						majorSize.height());
					majorLabel->moveToLeft(0, 0);
					minorLabel->moveToRight(
						0,
						st::channelEarnOverviewMinorLabelSkip);
				}, box->lifetime());

				Ui::AddSkip(box->verticalLayout());
				box->addRow(object_ptr<Ui::CenterWrap<>>(
					box,
					object_ptr<Ui::FlatLabel>(
						box,
						dateText,
						st::channelEarnHistorySubLabel)));
				Ui::AddSkip(box->verticalLayout());
				Ui::AddSkip(box->verticalLayout());
				Ui::AddSkip(box->verticalLayout());
				box->addRow(object_ptr<Ui::CenterWrap<>>(
					box,
					object_ptr<Ui::FlatLabel>(
						box,
						entry.in
							? tr::lng_channel_earn_history_in_about()
							: tr::lng_channel_earn_history_out(),
						st::channelEarnHistoryMajorLabel)));
				Ui::AddSkip(box->verticalLayout());
				if (entry.in) {
					Ui::AddSkip(box->verticalLayout());
				}

				if (!entry.recipient.isEmpty()) {
					const auto wrap = box->addRow(
						object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
							box,
							object_ptr<Ui::RoundButton>(
								box,
								rpl::single(QString()),
								st::channelEarnHistoryRecipientButton)));
					const auto container = wrap->entity();
					const auto label = Ui::CreateChild<Ui::FlatLabel>(
						container,
						rpl::single(recipient),
						st::channelEarnHistoryRecipientButtonLabel);
					label->setAttribute(Qt::WA_TransparentForMouseEvents);
					label->setBreakEverywhere(true);
					label->setTryMakeSimilarLines(true);
					label->resizeToWidth(container->width());
					label->sizeValue(
					) | rpl::start_with_next([=](const QSize &s) {
						const auto padding = QMargins(
							st::chatGiveawayPeerPadding.right(),
							st::chatGiveawayPeerPadding.top(),
							st::chatGiveawayPeerPadding.right(),
							st::chatGiveawayPeerPadding.top());
						container->resize(
							container->width(),
							(Rect(s) + padding).height());
						label->moveToLeft(0, padding.top());
					}, container->lifetime());
					container->setClickedCallback([=] {
						QGuiApplication::clipboard()->setText(recipient.text);
						box->showToast(tr::lng_text_copied(tr::now));
					});
				}
				if (entry.in) {
					const auto peerBubble = box->addRow(
						object_ptr<Ui::CenterWrap<>>(
							box,
							object_ptr<Ui::RpWidget>(box)))->entity();
					peerBubble->setAttribute(
						Qt::WA_TransparentForMouseEvents);
					const auto left = Ui::CreateChild<Ui::UserpicButton>(
						peerBubble,
						peer,
						st::uploadUserpicButton);
					const auto right = Ui::CreateChild<Ui::FlatLabel>(
						peerBubble,
						Info::Profile::NameValue(peer),
						st::channelEarnHistoryLabel);
					rpl::combine(
						left->sizeValue(),
						right->sizeValue()
					) | rpl::start_with_next([=](
							const QSize &leftSize,
							const QSize &rightSize) {
						const auto padding = QMargins(
							st::chatGiveawayPeerPadding.left() * 2,
							st::chatGiveawayPeerPadding.top(),
							st::chatGiveawayPeerPadding.right(),
							st::chatGiveawayPeerPadding.bottom());
						peerBubble->resize(
							leftSize.width()
								+ rightSize.width()
								+ rect::m::sum::h(padding),
							leftSize.height());
						left->moveToLeft(0, 0);
						right->moveToRight(padding.right(), padding.top());
						const auto maxRightSize = box->width()
							- rect::m::sum::h(st::boxRowPadding)
							- rect::m::sum::h(padding)
							- leftSize.width();
						if (rightSize.width() > maxRightSize) {
							right->resizeToWidth(maxRightSize);
						}
					}, peerBubble->lifetime());
					peerBubble->paintRequest(
					) | rpl::start_with_next([=] {
						auto p = QPainter(peerBubble);
						auto hq = PainterHighQualityEnabler(p);
						p.setPen(Qt::NoPen);
						p.setBrush(st::windowBgOver);
						const auto rect = peerBubble->rect();
						const auto radius = rect.height() / 2;
						p.drawRoundedRect(rect, radius, radius);
					}, peerBubble->lifetime());
				}
				Ui::AddSkip(box->verticalLayout());
				Ui::AddSkip(box->verticalLayout());
				box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
			};

			button->setClickedCallback([=] {
				_show->showBox(Box(detailsBox));
			});
			wrap->geometryValue(
			) | rpl::start_with_next([=](const QRect &g) {
				const auto &padding = st::boxRowPadding;
				const auto majorTop = (g.height() - majorLabel->height()) / 2;
				minorLabel->moveToRight(
					padding.right(),
					majorTop + st::channelEarnHistoryMinorLabelSkip);
				majorLabel->moveToRight(
					padding.right() + minorLabel->width(),
					majorTop);
				const auto rightWrapPadding = rect::m::sum::h(padding)
					+ minorLabel->width()
					+ majorLabel->width();
				wrap->setPadding(
					st::channelEarnHistoryOuter
						+ QMargins(padding.left(), 0, rightWrapPadding, 0));
				button->resize(g.size());
				button->lower();
			}, wrap->lifetime());
		};
		const auto randomRecipient = [&] { // Debug.
			const auto format = QUuid::StringFormat::Id128;
			return (QUuid::createUuid().toString(format)
				+ QUuid::createUuid().toString(format)).mid(0, 48);
		};
		addHistoryEntry(
			{
				.from = base::unixtime::now(),
				.to = base::unixtime::now() - base::RandomIndex(200000),
				.value = base::RandomIndex(1000000) / 1000.,
				.in = true,
			},
			tr::lng_channel_earn_history_in);
		addHistoryEntry(
			{
				.from = base::unixtime::now(),
				.recipient = randomRecipient(),
				.value = base::RandomIndex(1000000) / 1000.,
			},
			tr::lng_channel_earn_history_out);
		addHistoryEntry(
			{
				.from = base::unixtime::now(),
				.to = base::unixtime::now() - base::RandomIndex(200000),
				.value = base::RandomIndex(1000000) / 1000.,
				.in = true,
			},
			tr::lng_channel_earn_history_in);
		addHistoryEntry(
			{
				.from = base::unixtime::now(),
				.recipient = randomRecipient(),
				.value = base::RandomIndex(1000000) / 1000.,
			},
			tr::lng_channel_earn_history_out);
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

