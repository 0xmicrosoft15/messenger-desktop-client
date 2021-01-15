/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/invite_link_buttons.h"

#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"

namespace Ui {
namespace {

class JoinedCountButton final : public AbstractButton {
public:
	using AbstractButton::AbstractButton;

	void onStateChanged(State was, StateChangeSource source) override {
		update();
	}
};

} // namespace

void AddCopyShareLinkButtons(
		not_null<VerticalLayout*> container,
		Fn<void()> copyLink,
		Fn<void()> shareLink) {
	const auto wrap = container->add(
		object_ptr<FixedHeightWidget>(
			container,
			st::inviteLinkButton.height),
		st::inviteLinkButtonsPadding);
	const auto copy = CreateChild<RoundButton>(
		wrap,
		tr::lng_group_invite_copy(),
		st::inviteLinkCopy);
	copy->setTextTransform(RoundButton::TextTransform::NoTransform);
	copy->setClickedCallback(copyLink);
	const auto share = CreateChild<RoundButton>(
		wrap,
		tr::lng_group_invite_share(),
		st::inviteLinkShare);
	share->setTextTransform(RoundButton::TextTransform::NoTransform);
	share->setClickedCallback(shareLink);

	wrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto buttonWidth = (width - st::inviteLinkButtonsSkip) / 2;
		copy->setFullWidth(buttonWidth);
		share->setFullWidth(buttonWidth);
		copy->moveToLeft(0, 0, width);
		share->moveToRight(0, 0, width);
	}, wrap->lifetime());
}

not_null<AbstractButton*> AddJoinedCountButton(
		not_null<VerticalLayout*> container,
		rpl::producer<JoinedCountContent> content,
		style::margins padding) {
	struct State {
		JoinedCountContent content;
		QString phrase;
		int addedWidth = 0;
	};
	const auto wrap = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			st::inviteLinkUserpics.size),
		padding);
	const auto result = CreateChild<JoinedCountButton>(wrap);
	const auto state = result->lifetime().make_state<State>();
	std::move(
		content
	) | rpl::start_with_next([=](JoinedCountContent &&content) {
		state->content = std::move(content);
		result->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			!state->content.count);
		const auto &st = st::inviteLinkUserpics;
		const auto imageWidth = !state->content.userpics.isNull()
			? state->content.userpics.width() / style::DevicePixelRatio()
			: !state->content.count
			? 0
			: ((std::min(state->content.count, 3) - 1) * (st.size - st.shift)
				+ st.size);
		state->addedWidth = imageWidth
			? (imageWidth + st::inviteLinkUserpicsSkip)
			: 0;
		state->phrase = state->content.count
			? tr::lng_group_invite_joined(
				tr::now,
				lt_count_decimal,
				state->content.count)
			: tr::lng_group_invite_no_joined(tr::now);
		const auto fullWidth = st::inviteLinkJoinedFont->width(state->phrase)
			+ state->addedWidth;
		result->resize(fullWidth, st.size);
		result->move((wrap->width() - fullWidth) / 2, 0);
		result->update();
	}, result->lifetime());

	result->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(result);
		if (!state->content.userpics.isNull()) {
			p.drawImage(0, 0, state->content.userpics);
		}
		const auto &font = st::inviteLinkJoinedFont;
		p.setPen(state->content.count
			? st::defaultLinkButton.color
			: st::windowSubTextFg);
		p.setFont((result->isOver() || result->isDown())
			? font->underline()
			: font);
		const auto top = (result->height() - font->height) / 2;
		p.drawText(
			state->addedWidth,
			top + font->ascent,
			state->phrase);
	}, result->lifetime());

	wrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		result->move((width - result->width()) / 2, 0);
	}, wrap->lifetime());

	return result;
}

} // namespace Ui
