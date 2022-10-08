/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "dialogs/dialogs_key.h"
#include "ui/unread_badge.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Session;
class Folder;
class ForumTopic;
class CloudImageView;
} // namespace Data

namespace HistoryUnreadThings {
enum class AddType;
struct All;
class Proxy;
class ConstProxy;
} // namespace HistoryUnreadThings

namespace Ui {
} // namespace Ui

namespace Dialogs::Ui {
using namespace ::Ui;
struct PaintContext;
} // namespace Dialogs::Ui

namespace Dialogs {

class Row;
class IndexedList;
class MainList;

struct RowsByLetter {
	not_null<Row*> main;
	base::flat_map<QChar, not_null<Row*>> letters;
};

enum class SortMode {
	Date    = 0x00,
	Name    = 0x01,
	Add     = 0x02,
};

struct PositionChange {
	int from = -1;
	int to = -1;
};

struct UnreadState {
	int messages = 0;
	int messagesMuted = 0;
	int chats = 0;
	int chatsMuted = 0;
	int marks = 0;
	int marksMuted = 0;
	bool known = false;

	UnreadState &operator+=(const UnreadState &other) {
		messages += other.messages;
		messagesMuted += other.messagesMuted;
		chats += other.chats;
		chatsMuted += other.chatsMuted;
		marks += other.marks;
		marksMuted += other.marksMuted;
		return *this;
	}
	UnreadState &operator-=(const UnreadState &other) {
		messages -= other.messages;
		messagesMuted -= other.messagesMuted;
		chats -= other.chats;
		chatsMuted -= other.chatsMuted;
		marks -= other.marks;
		marksMuted -= other.marksMuted;
		return *this;
	}

	bool empty() const {
		return !messages && !chats && !marks;
	}
};

inline UnreadState operator+(const UnreadState &a, const UnreadState &b) {
	auto result = a;
	result += b;
	return result;
}

inline UnreadState operator-(const UnreadState &a, const UnreadState &b) {
	auto result = a;
	result -= b;
	return result;
}

class Entry : public base::has_weak_ptr {
public:
	enum class Type : uchar {
		History,
		Folder,
		ForumTopic,
	};
	Entry(not_null<Data::Session*> owner, Type type);
	Entry(const Entry &other) = delete;
	Entry &operator=(const Entry &other) = delete;
	virtual ~Entry();

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	History *asHistory();
	Data::Folder *asFolder();
	Data::ForumTopic *asTopic();

	PositionChange adjustByPosInChatList(
		FilterId filterId,
		not_null<MainList*> list);
	[[nodiscard]] bool inChatList(FilterId filterId = 0) const {
		return _chatListLinks.contains(filterId);
	}
	RowsByLetter *chatListLinks(FilterId filterId);
	const RowsByLetter *chatListLinks(FilterId filterId) const;
	[[nodiscard]] int posInChatList(FilterId filterId) const;
	not_null<Row*> addToChatList(
		FilterId filterId,
		not_null<MainList*> list);
	void removeFromChatList(
		FilterId filterId,
		not_null<MainList*> list);
	void removeChatListEntryByLetter(FilterId filterId, QChar letter);
	void addChatListEntryByLetter(
		FilterId filterId,
		QChar letter,
		not_null<Row*> row);
	void updateChatListEntry();
	[[nodiscard]] bool isPinnedDialog(FilterId filterId) const {
		return lookupPinnedIndex(filterId) != 0;
	}
	void cachePinnedIndex(FilterId filterId, int index);
	[[nodiscard]] bool isTopPromoted() const;
	[[nodiscard]] uint64 sortKeyInChatList(FilterId filterId) const {
		return filterId
			? computeSortPosition(filterId)
			: _sortKeyInChatList;
	}
	void updateChatListSortPosition();
	void setChatListTimeId(TimeId date);
	virtual void updateChatListExistence();
	bool needUpdateInChatList() const;
	virtual TimeId adjustedChatListTimeId() const;

	void setUnreadThingsKnown();
	[[nodiscard]] HistoryUnreadThings::Proxy unreadMentions();
	[[nodiscard]] HistoryUnreadThings::ConstProxy unreadMentions() const;
	[[nodiscard]] HistoryUnreadThings::Proxy unreadReactions();
	[[nodiscard]] HistoryUnreadThings::ConstProxy unreadReactions() const;

	virtual int fixedOnTopIndex() const = 0;
	static constexpr auto kArchiveFixOnTopIndex = 1;
	static constexpr auto kTopPromotionFixOnTopIndex = 2;

	virtual bool shouldBeInChatList() const = 0;
	virtual int chatListUnreadCount() const = 0;
	virtual bool chatListUnreadMark() const = 0;
	virtual bool chatListMutedBadge() const = 0;
	virtual UnreadState chatListUnreadState() const = 0;
	virtual HistoryItem *chatListMessage() const = 0;
	virtual bool chatListMessageKnown() const = 0;
	virtual void requestChatListMessage() = 0;
	virtual const QString &chatListName() const = 0;
	virtual const QString &chatListNameSortKey() const = 0;
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
		std::shared_ptr<Data::CloudImageView> &view,
		const Ui::PaintContext &context) const = 0;

	[[nodiscard]] TimeId chatListTimeId() const {
		return _timeId;
	}

	[[nodiscard]] const Ui::Text::String &chatListNameText() const;
	[[nodiscard]] Ui::PeerBadge &chatListBadge() const {
		return _chatListBadge;
	}

protected:
	void notifyUnreadStateChange(const UnreadState &wasState);
	auto unreadStateChangeNotifier(bool required) {
		const auto notify = required && inChatList();
		const auto wasState = notify ? chatListUnreadState() : UnreadState();
		return gsl::finally([=] {
			if (notify) {
				notifyUnreadStateChange(wasState);
			}
		});
	}

	[[nodiscard]] int lookupPinnedIndex(FilterId filterId) const;

	void cacheTopPromoted(bool promoted);

	[[nodiscard]] const base::flat_set<MsgId> &unreadMentionsIds() const;
	[[nodiscard]] const base::flat_set<MsgId> &unreadReactionsIds() const;

private:
	enum class Flag : uchar {
		IsTopPromoted = 0x01,
		UnreadThingsKnown = 0x02,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; }

	virtual void changedChatListPinHook();
	void pinnedIndexChanged(FilterId filterId, int was, int now);
	[[nodiscard]] uint64 computeSortPosition(FilterId filterId) const;

	[[nodiscard]] virtual int chatListNameVersion() const = 0;

	void setChatListExistence(bool exists);
	not_null<Row*> mainChatListLink(FilterId filterId) const;
	Row *maybeMainChatListLink(FilterId filterId) const;

	const not_null<Data::Session*> _owner;
	base::flat_map<FilterId, RowsByLetter> _chatListLinks;
	uint64 _sortKeyInChatList = 0;
	uint64 _sortKeyByDate = 0;
	base::flat_map<FilterId, int> _pinnedIndex;
	std::unique_ptr<HistoryUnreadThings::All> _unreadThings;
	mutable Ui::PeerBadge _chatListBadge;
	mutable Ui::Text::String _chatListNameText;
	mutable int _chatListNameVersion = 0;
	TimeId _timeId = 0;
	base::flags<Flag> _flags;
	const Type _type;

};

} // namespace Dialogs
