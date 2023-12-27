/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_main_list.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
class SavedSublist;

class SavedMessages final {
public:
	explicit SavedMessages(not_null<Session*> owner);
	~SavedMessages();

	[[nodiscard]] bool supported() const;

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] not_null<Dialogs::MainList*> chatsList();
	[[nodiscard]] not_null<SavedSublist*> sublist(not_null<PeerData*> peer);

	void loadMore();
	void loadMore(not_null<SavedSublist*> sublist);

private:
	const not_null<Session*> _owner;

	Dialogs::MainList _chatsList;
	base::flat_map<
		not_null<PeerData*>,
		std::unique_ptr<SavedSublist>> _sublists;

	base::flat_map<not_null<SavedSublist*>, mtpRequestId> _loadMoreRequests;
	mtpRequestId _loadMoreRequestId = 0;

	TimeId _offsetDate = 0;
	MsgId _offsetId = 0;
	PeerData *_offsetPeer = nullptr;

	bool _unsupported = false;

};

} // namespace Data
