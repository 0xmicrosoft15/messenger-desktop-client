/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_main_list.h"

#include "observer_peer.h"
#include "history/history.h"

namespace Dialogs {

MainList::MainList(rpl::producer<int> pinnedLimit)
: _all(SortMode::Date)
, _pinned(1) {
	_unreadState.known = true;

	std::move(
		pinnedLimit
	) | rpl::start_with_next([=](int limit) {
		_pinned.setLimit(limit);
	}, _lifetime);

	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::NameChanged
	) | rpl::start_with_next([=](const Notify::PeerUpdate &update) {
		const auto peer = update.peer;
		const auto &oldLetters = update.oldNameFirstLetters;
		_all.peerNameChanged(FilterId(), peer, oldLetters);
		for (auto &[filterId, list] : _other) {
			list.peerNameChanged(filterId, peer, oldLetters);
		}
	}, _lifetime);
}

bool MainList::empty() const {
	return _all.empty();
}

bool MainList::loaded() const {
	return _loaded;
}

void MainList::setLoaded(bool loaded) {
	_loaded = loaded;
}

void MainList::clear() {
	_all.clear();
	_other.clear();
	_unreadState = UnreadState();
}

void MainList::unreadStateChanged(
		const UnreadState &wasState,
		const UnreadState &nowState) {
	_unreadState += nowState - wasState;
}

void MainList::unreadEntryChanged(
		const Dialogs::UnreadState &state,
		bool added) {
	if (added) {
		_unreadState += state;
	} else {
		_unreadState -= state;
	}
}

UnreadState MainList::unreadState() const {
	return _unreadState;
}

not_null<IndexedList*> MainList::indexed(FilterId filterId) {
	if (!filterId) {
		return &_all;
	}
	const auto i = _other.find(filterId);
	if (i != end(_other)) {
		return &i->second;
	}
	return &_other.emplace(filterId, SortMode::Date).first->second;
}

not_null<const IndexedList*> MainList::indexed() const {
	return &_all;
}

not_null<PinnedList*> MainList::pinned() {
	return &_pinned;
}

not_null<const PinnedList*> MainList::pinned() const {
	return &_pinned;
}

} // namespace Dialogs
