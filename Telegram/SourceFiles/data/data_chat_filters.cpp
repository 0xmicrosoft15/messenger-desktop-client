/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat_filters.h"

#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "dialogs/dialogs_main_list.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Data {

ChatFilter::ChatFilter(
	FilterId id,
	const QString &title,
	Flags flags,
	base::flat_set<not_null<History*>> always,
	base::flat_set<not_null<History*>> never)
: _id(id)
, _title(title)
, _always(std::move(always))
, _never(std::move(never))
, _flags(flags) {
}

ChatFilter ChatFilter::FromTL(
		const MTPDialogFilter &data,
		not_null<Session*> owner) {
	return data.match([&](const MTPDdialogFilter &data) {
		const auto flags = (data.is_contacts() ? Flag::Contacts : Flag(0))
			| (data.is_non_contacts() ? Flag::NonContacts : Flag(0))
			| (data.is_groups() ? Flag::Groups : Flag(0))
			| (data.is_broadcasts() ? Flag::Broadcasts : Flag(0))
			| (data.is_bots() ? Flag::Bots : Flag(0))
			| (data.is_exclude_muted() ? Flag::NoMuted : Flag(0))
			| (data.is_exclude_read() ? Flag::NoRead : Flag(0))
			| (data.is_exclude_archived() ? Flag::NoArchive : Flag(0));
		auto &&to_histories = ranges::view::transform([&](
				const MTPInputPeer &data) {
			const auto peer = data.match([&](const MTPDinputPeerUser &data) {
				const auto user = owner->user(data.vuser_id().v);
				user->setAccessHash(data.vaccess_hash().v);
				return (PeerData*)user;
			}, [&](const MTPDinputPeerChat &data) {
				return (PeerData*)owner->chat(data.vchat_id().v);
			}, [&](const MTPDinputPeerChannel &data) {
				const auto channel = owner->channel(data.vchannel_id().v);
				channel->setAccessHash(data.vaccess_hash().v);
				return (PeerData*)channel;
			}, [&](const auto &data) {
				return (PeerData*)nullptr;
			});
			return peer ? owner->history(peer).get() : nullptr;
		}) | ranges::view::filter([](History *history) {
			return history != nullptr;
		}) | ranges::view::transform([](History *history) {
			return not_null<History*>(history);
		});
		auto &&always = ranges::view::all(
			data.vinclude_peers().v
		) | to_histories;
		auto &&never = ranges::view::all(
			data.vexclude_peers().v
		) | to_histories;
		return ChatFilter(
			data.vid().v,
			qs(data.vtitle()),
			flags,
			{ always.begin(), always.end() },
			{ never.begin(), never.end() });
	});
}

MTPDialogFilter ChatFilter::tl() const {
	using TLFlag = MTPDdialogFilter::Flag;
	const auto flags = TLFlag(0)
		| ((_flags & Flag::Contacts) ? TLFlag::f_contacts : TLFlag(0))
		| ((_flags & Flag::NonContacts) ? TLFlag::f_non_contacts : TLFlag(0))
		| ((_flags & Flag::Groups) ? TLFlag::f_groups : TLFlag(0))
		| ((_flags & Flag::Broadcasts) ? TLFlag::f_broadcasts : TLFlag(0))
		| ((_flags & Flag::Bots) ? TLFlag::f_bots : TLFlag(0))
		| ((_flags & Flag::NoMuted) ? TLFlag::f_exclude_muted : TLFlag(0))
		| ((_flags & Flag::NoRead) ? TLFlag::f_exclude_read : TLFlag(0))
		| ((_flags & Flag::NoArchive)
			? TLFlag::f_exclude_archived
			: TLFlag(0));
	auto always = QVector<MTPInputPeer>();
	always.reserve(_always.size());
	for (const auto history : _always) {
		always.push_back(history->peer->input);
	}
	auto never = QVector<MTPInputPeer>();
	never.reserve(_never.size());
	for (const auto history : _never) {
		never.push_back(history->peer->input);
	}
	return MTP_dialogFilter(
		MTP_flags(flags),
		MTP_int(_id),
		MTP_string(_title),
		MTPstring(), // emoticon
		MTP_vector<MTPInputPeer>(),
		MTP_vector<MTPInputPeer>(always),
		MTP_vector<MTPInputPeer>(never));
}

FilterId ChatFilter::id() const {
	return _id;
}

QString ChatFilter::title() const {
	return _title;
}

ChatFilter::Flags ChatFilter::flags() const {
	return _flags;
}

const base::flat_set<not_null<History*>> &ChatFilter::always() const {
	return _always;
}

const base::flat_set<not_null<History*>> &ChatFilter::never() const {
	return _never;
}

bool ChatFilter::contains(not_null<History*> history) const {
	const auto flag = [&] {
		const auto peer = history->peer;
		if (const auto user = peer->asUser()) {
			return user->isBot()
				? Flag::Bots
				: user->isContact()
				? Flag::Contacts
				: Flag::NonContacts;
		} else if (const auto chat = peer->asChat()) {
			return Flag::Groups;
		} else if (const auto channel = peer->asChannel()) {
			if (channel->isBroadcast()) {
				return Flag::Broadcasts;
			} else {
				return Flag::Groups;
			}
		} else {
			Unexpected("Peer type in ChatFilter::contains.");
		}
	}();
	if (_never.contains(history)) {
		return false;
	}
	return false
		|| ((_flags & flag)
			&& (!(_flags & Flag::NoMuted) || !history->mute())
			&& (!(_flags & Flag::NoRead) || history->unreadCountForBadge())
			&& (!(_flags & Flag::NoArchive)
				|| (history->folderKnown() && !history->folder())))
		|| _always.contains(history);
}

ChatFilters::ChatFilters(not_null<Session*> owner) : _owner(owner) {
	//using Flag = ChatFilter::Flag;
	//const auto all = Flag::Contacts
	//	| Flag::NonContacts
	//	| Flag::Groups
	//	| Flag::Broadcasts
	//	| Flag::Bots
	//	| Flag::NoArchive;
	//_list.push_back(
	//	ChatFilter(1, "Unmuted", all | Flag::NoMuted, {}, {}));
	//_list.push_back(
	//	ChatFilter(2, "Unread", all | Flag::NoRead, {}, {}));
	load();
}

ChatFilters::~ChatFilters() = default;

not_null<Dialogs::MainList*> ChatFilters::chatsList(FilterId filterId) {
	auto &pointer = _chatsLists[filterId];
	if (!pointer) {
		pointer = std::make_unique<Dialogs::MainList>(
			filterId,
			rpl::single(1));
	}
	return pointer.get();
}

void ChatFilters::load() {
	load(false);
}

void ChatFilters::load(bool force) {
	if (_loadRequestId && !force) {
		return;
	}
	auto &api = _owner->session().api();
	api.request(_loadRequestId).cancel();
	_loadRequestId = api.request(MTPmessages_GetDialogFilters(
	)).done([=](const MTPVector<MTPDialogFilter> &result) {
		auto position = 0;
		auto changed = false;
		for (const auto &filter : result.v) {
			auto parsed = ChatFilter::FromTL(filter, _owner);
			const auto b = begin(_list) + position, e = end(_list);
			const auto i = ranges::find(b, e, parsed.id(), &ChatFilter::id);
			if (i == e) {
				applyInsert(std::move(parsed), position);
				changed = true;
			} else if (i == b) {
				if (applyChange(*b, std::move(parsed))) {
					changed = true;
				}
			} else {
				std::swap(*i, *b);
				applyChange(*b, std::move(parsed));
				changed = true;
			}
			++position;
		}
		while (position < _list.size()) {
			applyRemove(position);
			changed = true;
		}
		if (changed) {
			_listChanged.fire({});
		}
		_loadRequestId = 0;
	}).fail([=](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void ChatFilters::apply(const MTPUpdate &update) {
	update.match([&](const MTPDupdateDialogFilter &data) {
		if (const auto filter = data.vfilter()) {
			set(ChatFilter::FromTL(*filter, _owner));
		} else {
			remove(data.vid().v);
		}
	}, [&](const MTPDupdateDialogFilters &data) {
		load(true);
	}, [&](const MTPDupdateDialogFilterOrder &data) {
		if (applyOrder(data.vorder().v)) {
			_listChanged.fire({});
		} else {
			load(true);
		}
	}, [](auto&&) {
		Unexpected("Update in ChatFilters::apply.");
	});
}

void ChatFilters::set(ChatFilter filter) {
	if (!filter.id()) {
		return;
	}
	const auto i = ranges::find(_list, filter.id(), &ChatFilter::id);
	if (i == end(_list)) {
		applyInsert(std::move(filter), _list.size());
		_listChanged.fire({});
	} else if (applyChange(*i, std::move(filter))) {
		_listChanged.fire({});
	}
}

void ChatFilters::applyInsert(ChatFilter filter, int position) {
	Expects(position >= 0 && position <= _list.size());

	_list.insert(
		begin(_list) + position,
		ChatFilter(filter.id(), {}, {}, {}, {}));
	applyChange(*(begin(_list) + position), std::move(filter));
}

void ChatFilters::remove(FilterId id) {
	const auto i = ranges::find(_list, id, &ChatFilter::id);
	if (i == end(_list)) {
		return;
	}
	applyRemove(i - begin(_list));
	_listChanged.fire({});
}

void ChatFilters::applyRemove(int position) {
	Expects(position >= 0 && position < _list.size());

	const auto i = begin(_list) + position;
	applyChange(*i, ChatFilter(i->id(), {}, {}, {}, {}));
	_list.erase(i);
}

bool ChatFilters::applyChange(ChatFilter &filter, ChatFilter &&updated) {
	const auto rulesChanged = (filter.flags() != updated.flags())
		|| (filter.always() != updated.always())
		|| (filter.never() != updated.never());
	if (rulesChanged) {
		const auto feedHistory = [&](not_null<History*> history) {
			const auto now = updated.contains(history);
			const auto was = filter.contains(history);
			if (now != was) {
				if (now) {
					history->addToChatList(filter.id());
				} else {
					history->removeFromChatList(filter.id());
				}
			}
		};
		const auto feedList = [&](not_null<const Dialogs::MainList*> list) {
			for (const auto &entry : *list->indexed()) {
				if (const auto history = entry->history()) {
					feedHistory(history);
				}
			}
		};
		feedList(_owner->chatsList());
		const auto id = Data::Folder::kId;
		if (const auto folder = _owner->folderLoaded(id)) {
			feedList(folder->chatsList());
		}
	} else if (filter.title() == updated.title()) {
		return false;
	}
	filter = std::move(updated);
	return true;
}

bool ChatFilters::applyOrder(const QVector<MTPint> &order) {
	if (order.size() != _list.size()) {
		return false;
	} else if (_list.empty()) {
		return true;
	}
	auto indices = ranges::view::all(
		_list
	) | ranges::view::transform(
		&ChatFilter::id
	) | ranges::to_vector;
	auto b = indices.begin(), e = indices.end();
	for (const auto id : order) {
		const auto i = ranges::find(b, e, id.v);
		if (i == e) {
			return false;
		} else if (i != b) {
			std::swap(*i, *b);
		}
		++b;
	}
	auto changed = false;
	auto begin = _list.begin(), end = _list.end();
	for (const auto id : order) {
		const auto i = ranges::find(begin, end, id.v, &ChatFilter::id);
		Assert(i != end);
		if (i != begin) {
			changed = true;
			std::swap(*i, *begin);
		}
		++begin;
	}
	if (changed) {
		_listChanged.fire({});
	}
	return true;
}

const std::vector<ChatFilter> &ChatFilters::list() const {
	return _list;
}

rpl::producer<> ChatFilters::changed() const {
	return _listChanged.events();
}

void ChatFilters::refreshHistory(not_null<History*> history) {
	_refreshHistoryRequests.fire_copy(history);
}

auto ChatFilters::refreshHistoryRequests() const
-> rpl::producer<not_null<History*>> {
	return _refreshHistoryRequests.events();
}


} // namespace Data
