/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

#include "dialogs/dialogs_key.h"

class AuthSession;

namespace Data {
class Session;
class Folder;
} // namespace Data

namespace Dialogs {

class Row;
class IndexedList;
using RowsByLetter = base::flat_map<QChar, not_null<Row*>>;

enum class SortMode {
	Date = 0x00,
	Name = 0x01,
	Add  = 0x02,
};

enum class Mode {
	All       = 0x00,
	Important = 0x01,
};

struct PositionChange {
	int from = -1;
	int to = -1;
};

class Entry {
public:
	Entry(not_null<Data::Session*> owner, const Key &key);
	Entry(const Entry &other) = delete;
	Entry &operator=(const Entry &other) = delete;
	virtual ~Entry() = default;

	Data::Session &owner() const;
	AuthSession &session() const;

	PositionChange adjustByPosInChatList(Mode list);
	bool inChatList(Mode list) const {
		return !chatListLinks(list).empty();
	}
	int posInChatList(Mode list) const;
	not_null<Row*> addToChatList(Mode list);
	void removeFromChatList(Mode list);
	void removeChatListEntryByLetter(Mode list, QChar letter);
	void addChatListEntryByLetter(
		Mode list,
		QChar letter,
		not_null<Row*> row);
	void updateChatListEntry() const;
	bool isPinnedDialog() const {
		return _pinnedIndex > 0;
	}
	void cachePinnedIndex(int index);
	bool isProxyPromoted() const {
		return _isProxyPromoted;
	}
	virtual bool useProxyPromotion() const = 0;
	void cacheProxyPromoted(bool promoted);
	uint64 sortKeyInChatList() const {
		return _sortKeyInChatList;
	}
	void updateChatListSortPosition();
	void setChatListTimeId(TimeId date);
	virtual void updateChatListExistence();
	bool needUpdateInChatList() const;
	virtual TimeId adjustedChatListTimeId() const;

	virtual bool toImportant() const = 0;
	virtual bool shouldBeInChatList() const = 0;
	virtual int chatListUnreadCount() const = 0;
	virtual bool chatListUnreadMark() const = 0;
	virtual bool chatListMutedBadge() const = 0;
	virtual HistoryItem *chatListMessage() const = 0;
	virtual bool chatListMessageKnown() const = 0;
	virtual void requestChatListMessage() = 0;
	virtual const QString &chatListName() const = 0;
	virtual const base::flat_set<QString> &chatListNameWords() const = 0;
	virtual const base::flat_set<QChar> &chatListFirstLetters() const = 0;

	virtual bool folderKnown() const {
		return true;
	}
	virtual Data::Folder *folder() const {
		return nullptr;
	}

	virtual void loadUserpic() = 0;
	virtual void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const = 0;
	void paintUserpicLeft(
			Painter &p,
			int x,
			int y,
			int w,
			int size) const {
		paintUserpic(p, rtl() ? (w - x - size) : x, y, size);
	}

	TimeId chatListTimeId() const {
		return _timeId;
	}

	mutable const HistoryItem *textCachedFor = nullptr; // cache
	mutable Text lastItemTextCache;

private:
	virtual void changedInChatListHook(Mode list, bool added);
	virtual void changedChatListPinHook();

	void setChatListExistence(bool exists);
	RowsByLetter &chatListLinks(Mode list);
	const RowsByLetter &chatListLinks(Mode list) const;
	Row *mainChatListLink(Mode list) const;

	not_null<IndexedList*> myChatsList(Mode list) const;

	not_null<Data::Session*> _owner;
	Dialogs::Key _key;
	RowsByLetter _chatListLinks[2];
	uint64 _sortKeyInChatList = 0;
	int _pinnedIndex = 0;
	bool _isProxyPromoted = false;
	TimeId _timeId = 0;

};

} // namespace Dialogs
