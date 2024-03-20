/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_about_view.h"

#include "chat_helpers/stickers_lottie.h"
#include "core/click_handler_types.h"
#include "data/business/data_business_common.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/media/history_view_service_box.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_item_reply_markup.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

class PremiumRequiredBox final : public ServiceBoxContent {
public:
	explicit PremiumRequiredBox(not_null<Element*> parent);
	~PremiumRequiredBox();

	int width() override;
	int top() override;
	QSize size() override;
	QString title() override;
	TextWithEntities subtitle() override;
	int buttonSkip() override;
	rpl::producer<QString> button() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) override;
	ClickHandlerPtr createViewLink() override;

	bool hideServiceText() override {
		return true;
	}

	void stickerClearLoopPlayed() override;
	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

private:
	const not_null<Element*> _parent;

};

class ChatIntroBox final : public ServiceBoxContent {
public:
	ChatIntroBox(not_null<Element*> parent, Data::ChatIntro data);
	~ChatIntroBox();

	int width() override;
	int top() override;
	QSize size() override;
	QString title() override;
	TextWithEntities subtitle() override;
	int buttonSkip() override;
	rpl::producer<QString> button() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) override;
	ClickHandlerPtr createViewLink() override;

	bool hideServiceText() override {
		return true;
	}

	void stickerClearLoopPlayed() override;
	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

private:
	const not_null<Element*> _parent;
	const Data::ChatIntro _data;
	mutable std::optional<Sticker> _sticker;

};

PremiumRequiredBox::PremiumRequiredBox(not_null<Element*> parent)
: _parent(parent) {
}

PremiumRequiredBox::~PremiumRequiredBox() = default;

int PremiumRequiredBox::width() {
	return st::premiumRequiredWidth;
}

int PremiumRequiredBox::top() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

QSize PremiumRequiredBox::size() {
	return { st::msgServicePhotoWidth, st::msgServicePhotoWidth };
}

QString PremiumRequiredBox::title() {
	return QString();
}

int PremiumRequiredBox::buttonSkip() {
	return st::storyMentionButtonSkip;
}

rpl::producer<QString> PremiumRequiredBox::button() {
	return tr::lng_send_non_premium_go();
}

TextWithEntities PremiumRequiredBox::subtitle() {
	return _parent->data()->notificationText();
}

ClickHandlerPtr PremiumRequiredBox::createViewLink() {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			Settings::ShowPremium(controller, u"require_premium"_q);
		}
	});
}

void PremiumRequiredBox::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	p.setBrush(context.st->msgServiceBg()); // ?
	p.setPen(Qt::NoPen);
	p.drawEllipse(geometry);
	st::premiumRequiredIcon.paintInCenter(p, geometry);
}

void PremiumRequiredBox::stickerClearLoopPlayed() {
}

std::unique_ptr<StickerPlayer> PremiumRequiredBox::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

bool PremiumRequiredBox::hasHeavyPart() {
	return false;
}

void PremiumRequiredBox::unloadHeavyPart() {
}

ChatIntroBox::ChatIntroBox(not_null<Element*> parent, Data::ChatIntro data)
: _parent(parent)
, _data(data) {
	if (const auto document = data.sticker) {
		if (const auto sticker = document->sticker()) {
			const auto skipPremiumEffect = true;
			_sticker.emplace(_parent, document, skipPremiumEffect, _parent);
			_sticker->initSize(st::chatIntroStickerSize);
			_sticker->setCustomCachingTag(
				ChatHelpers::StickerLottieSize::ChatIntroHelloSticker);
		}
	}
}

ChatIntroBox::~ChatIntroBox() = default;

int ChatIntroBox::width() {
	return st::chatIntroWidth;
}

int ChatIntroBox::top() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

QSize ChatIntroBox::size() {
	return { st::msgServicePhotoWidth, st::msgServicePhotoWidth };
}

QString ChatIntroBox::title() {
	return _data ? _data.title : tr::lng_chat_intro_default_title(tr::now);
}

int ChatIntroBox::buttonSkip() {
	return st::storyMentionButtonSkip;
}

rpl::producer<QString> ChatIntroBox::button() {
	return nullptr;
}

TextWithEntities ChatIntroBox::subtitle() {
	return {
		(_data
			? _data.description
			: tr::lng_chat_intro_default_message(tr::now))
	};
}

ClickHandlerPtr ChatIntroBox::createViewLink() {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			Settings::ShowPremium(controller, u"require_premium"_q);
		}
	});
}

void ChatIntroBox::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	if (_sticker) {
		_sticker->draw(p, context, geometry);
	}
}

void ChatIntroBox::stickerClearLoopPlayed() {
	if (_sticker) {
		_sticker->stickerClearLoopPlayed();
	}
}

std::unique_ptr<StickerPlayer> ChatIntroBox::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _sticker
		? _sticker->stickerTakePlayer(data, replacements)
		: nullptr;
}

bool ChatIntroBox::hasHeavyPart() {
	return _sticker && _sticker->hasHeavyPart();
}

void ChatIntroBox::unloadHeavyPart() {
	if (_sticker) {
		_sticker->unloadHeavyPart();
	}
}

} // namespace

AboutView::AboutView(
	not_null<History*> history,
	not_null<ElementDelegate*> delegate)
: _history(history)
, _delegate(delegate) {
}

AboutView::~AboutView() {
	setItem({}, nullptr);
}

not_null<History*> AboutView::history() const {
	return _history;
}

Element *AboutView::view() const {
	return _item.get();
}

HistoryItem *AboutView::item() const {
	if (const auto element = view()) {
		return element->data();
	}
	return nullptr;
}

bool AboutView::refresh() {
	const auto bot = _history->peer->asUser();
	const auto info = bot ? bot->botInfo.get() : nullptr;
	if (!info) {
		if (bot
			&& bot->meRequiresPremiumToWrite()
			&& !bot->session().premium()
			&& _history->isDisplayedEmpty()) {
			if (_item) {
				return false;
			}
			_item = makePremiumRequired();
			return true;
		}
		if (_item) {
			_item = {};
			return true;
		}
		_version = 0;
		return false;
	}
	const auto version = info->descriptionVersion;
	if (_version == version) {
		return false;
	}
	_version = version;
	_item = makeAboutBot(info);
	return true;
}

void AboutView::make(Data::ChatIntro data) {
	const auto item = _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeAboutView
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Local),
		.from = _history->peer->id,
	}, PreparedServiceText{ { data.description } });

	setItem(AdminLog::OwnedItem(_delegate, item), data.sticker);

	_item->overrideMedia(std::make_unique<ServiceBox>(
		_item.get(),
		std::make_unique<ChatIntroBox>(_item.get(), data)));
}

void AboutView::setItem(AdminLog::OwnedItem item, DocumentData *sticker) {
	if (const auto was = _item ? _item->data().get() : nullptr) {
		if (_sticker) {
			was->history()->owner().unregisterDocumentItem(_sticker, was);
		}
	}
	_item = std::move(item);
	_sticker = sticker;
	if (const auto now = _item ? _item->data().get() : nullptr) {
		if (_sticker) {
			now->history()->owner().registerDocumentItem(_sticker, now);
		}
	}
}

AdminLog::OwnedItem AboutView::makeAboutBot(not_null<BotInfo*> info) {
	const auto textWithEntities = TextUtilities::ParseEntities(
		info->description,
		Ui::ItemTextBotNoMonoOptions().flags);
	const auto make = [&](auto &&...args) {
		return _history->makeMessage({
			.id = _history->nextNonHistoryEntryId(),
			.flags = (MessageFlag::FakeAboutView
				| MessageFlag::FakeHistoryItem
				| MessageFlag::Local),
			.from = _history->peer->id,
		}, std::forward<decltype(args)>(args)...);
	};
	const auto item = info->document
		? make(info->document, textWithEntities)
		: info->photo
		? make(info->photo, textWithEntities)
		: make(textWithEntities, MTP_messageMediaEmpty());
	return AdminLog::OwnedItem(_delegate, item);
}

AdminLog::OwnedItem AboutView::makePremiumRequired() {
	const auto item = _history->makeMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeAboutView
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Local),
		.from = _history->peer->id,
	}, PreparedServiceText{ tr::lng_send_non_premium_text(
		tr::now,
		lt_user,
		Ui::Text::Bold(_history->peer->shortName()),
		Ui::Text::RichLangValue),
	});
	auto result = AdminLog::OwnedItem(_delegate, item);
	result->overrideMedia(std::make_unique<ServiceBox>(
		result.get(),
		std::make_unique<PremiumRequiredBox>(result.get())));
	return result;
}

} // namespace HistoryView
