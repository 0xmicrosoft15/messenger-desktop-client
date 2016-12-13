/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "window/section_widget.h"
#include "ui/widgets/scroll_area.h"

namespace Dialogs {
class Row;
class FakeRow;
class IndexedList;
} // namespace Dialogs

namespace Ui {
class IconButton;
class PopupMenu;
class DropdownMenu;
class FlatButton;
class LinkButton;
class FlatInput;
class CrossButton;
} // namespace Ui

enum DialogsSearchRequestType {
	DialogsSearchFromStart,
	DialogsSearchFromOffset,
	DialogsSearchPeerFromStart,
	DialogsSearchPeerFromOffset,
	DialogsSearchMigratedFromStart,
	DialogsSearchMigratedFromOffset,
};

class DialogsInner : public Ui::SplittedWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	DialogsInner(QWidget *parent, QWidget *main);

	void dialogsReceived(const QVector<MTPDialog> &dialogs);
	void addSavedPeersAfter(const QDateTime &date);
	void addAllSavedPeers();
	bool searchReceived(const QVector<MTPMessage> &result, DialogsSearchRequestType type, int32 fullCount);
	void peerSearchReceived(const QString &query, const QVector<MTPPeer> &result);
	void showMore(int32 pixels);

	void activate();

	void contactsReceived(const QVector<MTPContact> &result);

	void selectSkip(int32 direction);
	void selectSkipPage(int32 pixels, int32 direction);

	void createDialog(History *history);
	void dlgUpdated(Dialogs::Mode list, Dialogs::Row *row);
	void dlgUpdated(PeerData *peer, MsgId msgId);
	void removeDialog(History *history);

	void dragLeft();

	void clearFilter();
	void refresh(bool toTop = false);

	bool choosePeer();
	void saveRecentHashtags(const QString &text);

	void destroyData();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void scrollToPeer(const PeerId &peer, MsgId msgId);

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();
	int32 lastSearchDate() const;
	PeerData *lastSearchPeer() const;
	MsgId lastSearchId() const;
	MsgId lastSearchMigratedId() const;

	void setMouseSelection(bool mouseSelection, bool toTop = false);

	enum State {
		DefaultState = 0,
		FilteredState = 1,
		SearchedState = 2,
	};
	void setState(State newState);
	State state() const;
	bool hasFilteredResults() const;

	void searchInPeer(PeerData *peer);

	void onFilterUpdate(QString newFilter, bool force = false);
	void onHashtagFilterUpdate(QStringRef newFilter);

	PeerData *updateFromParentDrag(QPoint globalPos);

	void setLoadMoreCallback(base::lambda<void()> &&callback) {
		_loadMoreCallback = std_::move(callback);
	}
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_historyMuteUpdated(History *history);

	~DialogsInner();

public slots:
	void onParentGeometryChanged();
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void onPeerPhotoChanged(PeerData *peer);
	void onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow);

	void onMenuDestroyed(QObject*);

signals:
	void mustScrollTo(int scrollToTop, int scrollToBottom);
	void dialogMoved(int movedFrom, int movedTo);
	void searchMessages();
	void searchResultChosen();
	void cancelSearchInPeer();
	void completeHashtag(QString tag);
	void refreshHashtags();

protected:
	void paintRegion(Painter &p, const QRegion &region, bool paintingOther) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	struct ImportantSwitch;
	using DialogsList = std_::unique_ptr<Dialogs::IndexedList>;
	using FilteredDialogs = QVector<Dialogs::Row*>;
	using SearchResults = std_::vector_of_moveable<std_::unique_ptr<Dialogs::FakeRow>>;
	struct HashtagResult;
	using HashtagResults = std_::vector_of_moveable<std_::unique_ptr<HashtagResult>>;
	struct PeerSearchResult;
	using PeerSearchResults = std_::vector_of_moveable<std_::unique_ptr<PeerSearchResult>>;

	void mousePressReleased(Qt::MouseButton button);
	void clearIrrelevantState();
	void updateSelected() {
		updateSelected(mapFromGlobal(QCursor::pos()));
	}
	void updateSelected(QPoint localPos);
	void loadPeerPhotos(int visibleTop);
	void setImportantSwitchPressed(bool pressed);
	void setPressed(Dialogs::Row *pressed);
	void setHashtagPressed(int pressed);
	void setFilteredPressed(int pressed);
	void setPeerSearchPressed(int pressed);
	void setSearchedPressed(int pressed);
	bool isPressed() const {
		return _importantSwitchPressed || _pressed || (_hashtagPressed >= 0) || (_filteredPressed >= 0) || (_peerSearchPressed >= 0) || (_searchedPressed >= 0);
	}
	bool isSelected() const {
		return _importantSwitchSelected || _selected || (_hashtagSelected >= 0) || (_filteredSelected>= 0) || (_peerSearchSelected >= 0) || (_searchedSelected >= 0);
	}

	void itemRemoved(HistoryItem *item);
	enum class UpdateRowSection {
		Default = 0x01,
		Filtered = 0x02,
		PeerSearch = 0x04,
		MessageSearch = 0x08,
		All = 0x0F,
	};
	Q_DECLARE_FLAGS(UpdateRowSections, UpdateRowSection);
	Q_DECLARE_FRIEND_OPERATORS_FOR_FLAGS(UpdateRowSections);
	void updateDialogRow(PeerData *peer, MsgId msgId, QRect updateRect, UpdateRowSections sections = UpdateRowSection::All);

	int dialogsOffset() const;
	int filteredOffset() const;
	int peerSearchOffset() const;
	int searchedOffset() const;

	void paintDialog(QPainter &p, Dialogs::Row *dialog);
	void paintPeerSearchResult(Painter &p, const PeerSearchResult *result, int32 w, bool active, bool selected, bool onlyBackground, TimeMs ms) const;
	void paintSearchInPeer(Painter &p, int32 w, bool onlyBackground) const;

	void clearSelection();
	void clearSearchResults(bool clearPeerSearchResults = true);
	void updateSelectedRow(PeerData *peer = 0);

	Dialogs::IndexedList *shownDialogs() const {
		return (Global::DialogsMode() == Dialogs::Mode::Important) ? _dialogsImportant.get() : _dialogs.get();
	}

	DialogsList _dialogs;
	DialogsList _dialogsImportant;

	DialogsList _contactsNoDialogs;
	DialogsList _contacts;

	bool _mouseSelection = false;
	Qt::MouseButton _pressButton = Qt::LeftButton;

	std_::unique_ptr<ImportantSwitch> _importantSwitch;
	bool _importantSwitchSelected = false;
	bool _importantSwitchPressed = false;
	Dialogs::Row *_selected = nullptr;
	Dialogs::Row *_pressed = nullptr;

	int _visibleAreaHeight = 0;
	QString _filter, _hashtagFilter;

	HashtagResults _hashtagResults;
	int _hashtagSelected = -1;
	int _hashtagPressed = -1;
	bool _hashtagDeleteSelected = false;
	bool _hashtagDeletePressed = false;

	FilteredDialogs _filterResults;
	int _filteredSelected = -1;
	int _filteredPressed = -1;

	QString _peerSearchQuery;
	PeerSearchResults _peerSearchResults;
	int _peerSearchSelected = -1;
	int _peerSearchPressed = -1;

	SearchResults _searchResults;
	int _searchedCount = 0;
	int _searchedMigratedCount = 0;
	int _searchedSelected = -1;
	int _searchedPressed = -1;

	int _lastSearchDate = 0;
	PeerData *_lastSearchPeer = nullptr;
	MsgId _lastSearchId = 0;
	MsgId _lastSearchMigratedId = 0;

	State _state = DefaultState;

	ChildWidget<Ui::LinkButton> _addContactLnk;
	ChildWidget<Ui::IconButton> _cancelSearchInPeer;

	PeerData *_searchInPeer = nullptr;
	PeerData *_searchInMigrated = nullptr;
	PeerData *_menuPeer = nullptr;

	Ui::PopupMenu *_menu = nullptr;

	base::lambda<void()> _loadMoreCallback;

};

Q_DECLARE_OPERATORS_FOR_FLAGS(DialogsInner::UpdateRowSections);

class DialogsWidget : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	DialogsWidget(QWidget *parent);

	void updateDragInScroll(bool inScroll);

	void searchInPeer(PeerData *peer);

	void loadDialogs();
	void loadPinnedDialogs();
	void createDialog(History *history);
	void dlgUpdated(Dialogs::Mode list, Dialogs::Row *row);
	void dlgUpdated(PeerData *peer, MsgId msgId);

	void dialogsToUp();

	bool hasTopBarShadow() const {
		return true;
	}
	void showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params);
	void showFast();

	void destroyData();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void scrollToPeer(const PeerId &peer, MsgId msgId);

	void removeDialog(History *history);

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();

	void searchMessages(const QString &query, PeerData *inPeer = 0);
	void onSearchMore();

	void rpcClear() override {
		_inner->rpcClear();
		RPCSender::rpcClear();
	}

	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_historyMuteUpdated(History *history);

signals:
	void cancelled();

public slots:
	void onCancel();
	void onListScroll();
	void activate();
	void onFilterUpdate(bool force = false);
	bool onCancelSearch();
	void onCancelSearchInPeer();

	void onFilterCursorMoved(int from = -1, int to = -1);
	void onCompleteHashtag(QString tag);

	void onDialogMoved(int movedFrom, int movedTo);
	bool onSearchMessages(bool searchCache = false);
	void onNeedSearchMessages();

	void onChooseByDrag();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
private slots:
	void onCheckUpdateStatus();
#endif // TDESKTOP_DISABLE_AUTOUPDATE

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void animationCallback();
	void dialogsReceived(const MTPmessages_Dialogs &dialogs, mtpRequestId requestId);
	void pinnedDialogsReceived(const MTPmessages_PeerDialogs &dialogs, mtpRequestId requestId);
	void contactsReceived(const MTPcontacts_Contacts &result);
	void searchReceived(DialogsSearchRequestType type, const MTPmessages_Messages &result, mtpRequestId requestId);
	void peerSearchReceived(const MTPcontacts_Found &result, mtpRequestId requestId);

	void setSearchInPeer(PeerData *peer);
	void showMainMenu();
	void updateLockUnlockVisibility();
	void updateControlsGeometry();
	void updateForwardBar();

	bool _dragInScroll = false;
	bool _dragForward = false;
	QTimer _chooseByDragTimer;

	void unreadCountsReceived(const QVector<MTPDialog> &dialogs);
	bool dialogsFailed(const RPCError &error, mtpRequestId req);
	bool contactsFailed(const RPCError &error);
	bool searchFailed(DialogsSearchRequestType type, const RPCError &error, mtpRequestId req);
	bool peopleFailed(const RPCError &error, mtpRequestId req);

	bool _dialogsFull = false;
	int32 _dialogsOffsetDate = 0;
	MsgId _dialogsOffsetId = 0;
	PeerData *_dialogsOffsetPeer = nullptr;
	mtpRequestId _dialogsRequestId = 0;
	mtpRequestId _pinnedDialogsRequestId = 0;
	mtpRequestId _contactsRequestId = 0;
	bool _pinnedDialogsReceived = false;

	ChildWidget<Ui::IconButton> _forwardCancel = { nullptr };
	ChildWidget<Ui::IconButton> _mainMenuToggle;
	ChildWidget<Ui::FlatInput> _filter;
	ChildWidget<Ui::CrossButton> _cancelSearch;
	ChildWidget<Ui::IconButton> _lockUnlock;
	ChildWidget<Ui::ScrollArea> _scroll;
	ChildWidget<DialogsInner> _inner;
	ChildWidget<Ui::FlatButton> _updateTelegram = { nullptr };

	Animation _a_show;
	Window::SlideDirection _showDirection;
	QPixmap _cacheUnder, _cacheOver;

	PeerData *_searchInPeer = nullptr;
	PeerData *_searchInMigrated = nullptr;

	QTimer _searchTimer;

	QString _peerSearchQuery;
	bool _peerSearchFull = false;
	mtpRequestId _peerSearchRequest = 0;

	QString _searchQuery;
	bool _searchFull = false;
	bool _searchFullMigrated = false;
	mtpRequestId _searchRequest = 0;

	using SearchCache = QMap<QString, MTPmessages_Messages>;
	SearchCache _searchCache;

	using SearchQueries = QMap<mtpRequestId, QString>;
	SearchQueries _searchQueries;

	using PeerSearchCache = QMap<QString, MTPcontacts_Found>;
	PeerSearchCache _peerSearchCache;

	using PeerSearchQueries = QMap<mtpRequestId, QString>;
	PeerSearchQueries _peerSearchQueries;

};
