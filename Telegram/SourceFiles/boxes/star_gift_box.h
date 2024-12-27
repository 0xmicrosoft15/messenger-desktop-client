/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct UniqueGift;
struct GiftCode;
struct CreditsHistoryEntry;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Ui {

class GenericBox;
class VerticalLayout;

void ChooseStarGiftRecipient(
	not_null<Window::SessionController*> controller);

void ShowStarGiftBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

void AddUniqueGiftCover(
	not_null<VerticalLayout*> container,
	rpl::producer<Data::UniqueGift> data,
	rpl::producer<QString> subtitleOverride = nullptr);

void PaintPoints(
	QPainter &p,
	base::flat_map<float64, QImage> &cache,
	not_null<Text::CustomEmoji*> emoji,
	const Data::UniqueGift &gift,
	const QRect &rect,
	float64 shown = 1.);

struct StarGiftUpgradeArgs {
	not_null<Window::SessionController*> controller;
	base::required<uint64> stargiftId;
	Fn<void(bool)> ready;
	not_null<UserData*> user;
	MsgId itemId = 0;
	int cost = 0;
	bool canAddSender = false;
	bool canAddComment = false;
};
void ShowStarGiftUpgradeBox(StarGiftUpgradeArgs &&args);

void AddUniqueCloseButton(not_null<GenericBox*> box);

} // namespace Ui
