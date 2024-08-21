/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

using Participants = std::vector<not_null<PeerData*>>;

namespace Ui {

class Checkbox;
class VerticalLayout;

struct ExpandablePeerListController final {
	rpl::event_stream<bool> toggleRequestsFromTop;
	rpl::event_stream<bool> toggleRequestsFromInner;
	rpl::event_stream<bool> checkAllRequests;
	Fn<Participants()> collectRequests;
};

void AddExpandablePeerList(
	not_null<Ui::Checkbox*> checkbox,
	not_null<ExpandablePeerListController*> controller,
	not_null<Ui::VerticalLayout*> inner,
	const Participants &participants,
	bool handleSingle,
	bool hideRightButton);

} // namespace Ui
