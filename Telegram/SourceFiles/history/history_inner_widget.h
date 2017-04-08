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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/widgets/tooltip.h"
#include "ui/widgets/scroll_area.h"

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class PopupMenu;
} // namespace Ui

class HistoryWidget;
class HistoryInner : public TWidget, public Ui::AbstractTooltipShower, private base::Subscriber {
	Q_OBJECT

public:
	HistoryInner(HistoryWidget *historyWidget, gsl::not_null<Window::Controller*> controller, Ui::ScrollArea *scroll, History *history);

	void messagesReceived(PeerData *peer, const QVector<MTPMessage> &messages);
	void messagesReceivedDown(PeerData *peer, const QVector<MTPMessage> &messages);

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);

	TextWithEntities getSelectedText() const;

	void dragActionStart(const QPoint &screenPos, Qt::MouseButton button = Qt::LeftButton);
	void dragActionUpdate(const QPoint &screenPos);
	void dragActionFinish(const QPoint &screenPos, Qt::MouseButton button = Qt::LeftButton);
	void dragActionCancel();

	void touchScrollUpdated(const QPoint &screenPos);
	QPoint mapMouseToItem(QPoint p, HistoryItem *item);

	void recountHeight();
	void updateSize();

	void repaintItem(const HistoryItem *item);

	bool canCopySelected() const;
	bool canDeleteSelected() const;

	void getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const;
	void clearSelectedItems(bool onlyTextSelection = false);
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);
	void selectItem(HistoryItem *item);

	void updateBotInfo(bool recount = true);

	bool wasSelectedText() const;
	void setFirstLoading(bool loading);

	// updates history->scrollTopItem/scrollTopOffset
	void visibleAreaUpdated(int top, int bottom);

	int historyHeight() const;
	int historyScrollTop() const;
	int migratedTop() const;
	int historyTop() const;
	int historyDrawTop() const;
	int itemTop(const HistoryItem *item) const; // -1 if should not be visible, -2 if bad history()

	void notifyIsBotChanged();
	void notifyMigrateUpdated();

	// When inline keyboard has moved because of the edition of its item we want
	// to move scroll position so that mouse points to the same button row.
	int moveScrollFollowingInlineKeyboard(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop);

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

	~HistoryInner();

protected:
	bool focusNextPrevChild(bool next) override;

	bool event(QEvent *e) override; // calls touchEvent when necessary
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	public slots:
	void onUpdateSelected();
	void onParentGeometryChanged();

	void copyContextUrl();
	void copyContextImage();
	void cancelContextDownload();
	void showContextInFolder();
	void saveContextGif();
	void copyContextText();
	void copySelectedText();

	void onMenuDestroy(QObject *obj);
	void onTouchSelect();
	void onTouchScrollTimer();
	void onDragExec();

	private slots:
	void onScrollDateCheck();
	void onScrollDateHideByTimer();

private:
	void itemRemoved(HistoryItem *item);
	void savePhotoToFile(PhotoData *photo);
	void saveDocumentToFile(DocumentData *document);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	void adjustCurrent(int32 y) const;
	void adjustCurrent(int32 y, History *history) const;
	HistoryItem *prevItem(HistoryItem *item);
	HistoryItem *nextItem(HistoryItem *item);
	void updateDragSelection(HistoryItem *dragSelFrom, HistoryItem *dragSelTo, bool dragSelecting, bool force = false);

	void setToClipboard(const TextWithEntities &forClipboard, QClipboard::Mode mode = QClipboard::Clipboard);

	void toggleScrollDateShown();
	void repaintScrollDateCallback();
	bool displayScrollDate() const;
	void scrollDateHide();
	void keepScrollDateForNow();

	gsl::not_null<Window::Controller*> _controller;

	PeerData *_peer = nullptr;
	History *_migrated = nullptr;
	History *_history = nullptr;
	int _historyPaddingTop = 0;

	// with migrated history we perhaps do not need to display first _history message
	// (if last _migrated message and first _history message are both isGroupMigrate)
	// or at least we don't need to display first _history date (just skip it by height)
	int _historySkipHeight = 0;

	class BotAbout : public ClickHandlerHost {
	public:
		BotAbout(HistoryInner *parent, BotInfo *info) : info(info), _parent(parent) {
		}
		BotInfo *info = nullptr;
		int width = 0;
		int height = 0;
		QRect rect;

		// ClickHandlerHost interface
		void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
		void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	private:
		HistoryInner *_parent;

	};
	std::unique_ptr<BotAbout> _botAbout;

	HistoryWidget *_widget = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	mutable History *_curHistory = nullptr;
	mutable int _curBlock = 0;
	mutable int _curItem = 0;

	bool _firstLoading = false;

	style::cursor _cursor = style::cur_default;
	using SelectedItems = QMap<HistoryItem*, TextSelection>;
	SelectedItems _selected;
	void applyDragSelection();
	void applyDragSelection(SelectedItems *toItems) const;
	void addSelectionRange(SelectedItems *toItems, int32 fromblock, int32 fromitem, int32 toblock, int32 toitem, History *h) const;

	// Does any of the shown histories has this flag set.
	bool hasPendingResizedItems() const {
		return (_history && _history->hasPendingResizedItems()) || (_migrated && _migrated->hasPendingResizedItems());
	}

	enum DragAction {
		NoDrag = 0x00,
		PrepareDrag = 0x01,
		Dragging = 0x02,
		PrepareSelect = 0x03,
		Selecting = 0x04,
	};
	DragAction _dragAction = NoDrag;
	TextSelectType _dragSelType = TextSelectType::Letters;
	QPoint _dragStartPos, _dragPos;
	HistoryItem *_dragItem = nullptr;
	HistoryCursorState _dragCursorState = HistoryDefaultCursorState;
	uint16 _dragSymbol = 0;
	bool _dragWasInactive = false;

	QPoint _trippleClickPoint;
	QTimer _trippleClickTimer;

	ClickHandlerPtr _contextMenuLnk;

	HistoryItem *_dragSelFrom = nullptr;
	HistoryItem *_dragSelTo = nullptr;
	bool _dragSelecting = false;
	bool _wasSelectedText = false; // was some text selected in current drag action

								   // scroll by touch support (at least Windows Surface tablets)
	bool _touchScroll = false;
	bool _touchSelect = false;
	bool _touchInProgress = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	QTimer _touchSelectTimer;

	Ui::TouchScrollState _touchScrollState = Ui::TouchScrollState::Manual;
	bool _touchPrevPosValid = false;
	bool _touchWaitingAcceleration = false;
	QPoint _touchSpeed;
	TimeMs _touchSpeedTime = 0;
	TimeMs _touchAccelerationTime = 0;
	TimeMs _touchTime = 0;
	QTimer _touchScrollTimer;

	// context menu
	Ui::PopupMenu *_menu = nullptr;

	// save visible area coords for painting / pressing userpics
	int _visibleAreaTop = 0;
	int _visibleAreaBottom = 0;

	bool _scrollDateShown = false;
	Animation _scrollDateOpacity;
	SingleQueuedInvokation _scrollDateCheck;
	SingleTimer _scrollDateHideTimer;
	HistoryItem *_scrollDateLastItem = nullptr;
	int _scrollDateLastItemTop = 0;
	ClickHandlerPtr _scrollDateLink;

	enum class EnumItemsDirection {
		TopToBottom,
		BottomToTop,
	};
	// this function finds all history items that are displayed and calls template method
	// for each found message (in given direction) in the passed history with passed top offset
	//
	// method has "bool (*Method)(HistoryItem *item, int itemtop, int itembottom)" signature
	// if it returns false the enumeration stops immidiately
	template <bool TopToBottom, typename Method>
	void enumerateItemsInHistory(History *history, int historytop, Method method);

	template <EnumItemsDirection direction, typename Method>
	void enumerateItems(Method method) {
		constexpr auto TopToBottom = (direction == EnumItemsDirection::TopToBottom);
		if (TopToBottom && _migrated) {
			enumerateItemsInHistory<TopToBottom>(_migrated, migratedTop(), method);
		}
		enumerateItemsInHistory<TopToBottom>(_history, historyTop(), method);
		if (!TopToBottom && _migrated) {
			enumerateItemsInHistory<TopToBottom>(_migrated, migratedTop(), method);
		}
	}

	// this function finds all userpics on the left that are displayed and calls template method
	// for each found userpic (from the top to the bottom) using enumerateItems() method
	//
	// method has "bool (*Method)(HistoryMessage *message, int userpicTop)" signature
	// if it returns false the enumeration stops immidiately
	template <typename Method>
	void enumerateUserpics(Method method);

	// this function finds all date elements that are displayed and calls template method
	// for each found date element (from the bottom to the top) using enumerateItems() method
	//
	// method has "bool (*Method)(HistoryItem *item, int itemtop, int dateTop)" signature
	// if it returns false the enumeration stops immidiately
	template <typename Method>
	void enumerateDates(Method method);

};
