/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {

class GenericBox;

void FillPeerQrBox(
	not_null<Ui::GenericBox*> box,
	not_null<PeerData*> peer,
	std::optional<QString> customLink = std::nullopt,
	rpl::producer<QString> about = nullptr);

} // namespace Ui
