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

#include "abstractbox.h"
#include "core/lambda_wrap.h"
#include "core/observer.h"

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

namespace internal {
class ShareInner;
} // namespace internal

namespace Notify {
struct PeerUpdate;
} // namespace Notify

class ShareBox : public ItemListBox, public RPCSender {
	Q_OBJECT

public:
	using SubmitCallback = base::lambda_unique<void(const QVector<PeerData*> &)>;
	ShareBox(SubmitCallback &&callback);

private slots:
	void onFilterUpdate();
	void onFilterCancel();
	void onScroll();

	bool onSearchByUsername(bool searchCache = false);
	void onNeedSearchByUsername();

	void onShare();
	void onSelectedChanged();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void doSetInnerFocus() override;

private:
	void moveButtons();

	void peopleReceived(const MTPcontacts_Found &result, mtpRequestId requestId);
	bool peopleFailed(const RPCError &error, mtpRequestId requestId);

	SubmitCallback _callback;

	ChildWidget<internal::ShareInner> _inner;
	ChildWidget<InputField> _filter;
	ChildWidget<IconedButton> _filterCancel;

	ChildWidget<BoxButton> _share;
	ChildWidget<BoxButton> _cancel;

	ChildWidget<ScrollableBoxShadow> _topShadow;
	ChildWidget<ScrollableBoxShadow> _bottomShadow;

	QTimer _searchTimer;
	QString _peopleQuery;
	bool _peopleFull = false;
	mtpRequestId _peopleRequest = 0;

	using PeopleCache = QMap<QString, MTPcontacts_Found>;
	PeopleCache _peopleCache;

	using PeopleQueries = QMap<mtpRequestId, QString>;
	PeopleQueries _peopleQueries;

};

namespace internal {

class ShareInner : public ScrolledWidget, public RPCSender, public Notify::Observer {
	Q_OBJECT

public:
	ShareInner(QWidget *parent);

	QVector<PeerData*> selected() const;
	bool hasSelected() const;

	void peopleReceived(const QString &query, const QVector<MTPPeer> &people);

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;
	void updateFilter(QString filter = QString());

	~ShareInner();

signals:
	void selectAllQuery();
	void searchByUsername();
	void selectedChanged();

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	static constexpr int WideCacheScale = 4;
	struct Chat {
		Chat(PeerData *peer);
		PeerData *peer;
		Text name;
		bool selected = false;
		QPixmap wideUserpicCache;
		ColorAnimation nameFg;
		FloatAnimation selection;
		struct Icon {
			FloatAnimation fadeIn;
			FloatAnimation fadeOut;
			QPixmap wideCheckCache;
		};
		QList<Icon> icons;
	};
	void paintChat(Painter &p, Chat *chat, int index);
	void updateChat(PeerData *peer);
	void updateChatName(Chat *chat, PeerData *peer);
	void repaintChat(PeerData *peer);
	void removeFadeOutedIcons(Chat *chat);
	void prepareWideUserpicCache(Chat *chat);
	void prepareWideCheckIconCache(Chat::Icon *icon);
	void prepareWideCheckIcons();
	int chatIndex(PeerData *peer) const;
	void repaintChatAtIndex(int index);
	Chat *getChatAtIndex(int index);

	void loadProfilePhotos(int yFrom);
	void changeCheckState(Chat *chat);

	Chat *getChat(Dialogs::Row *row);
	void setActive(int active);
	void updateUpon(const QPoint &pos);

	void refresh();

	float64 _columnSkip = 0.;
	float64 _rowWidthReal = 0.;
	int _rowsLeft = 0;
	int _rowsTop = 0;
	int _rowWidth = 0;
	int _rowHeight = 0;
	int _columnCount = 4;
	int _active = -1;
	int _upon = -1;

	std_::unique_ptr<Dialogs::IndexedList> _chatsIndexed;
	QString _filter;
	using FilteredDialogs = QVector<Dialogs::Row*>;
	FilteredDialogs _filtered;

	QPixmap _wideCheckCache, _wideCheckIconCache;

	using DataMap = QMap<PeerData*, Chat*>;
	DataMap _dataMap;
	using SelectedChats = OrderedSet<PeerData*>;
	SelectedChats _selected;

	ChatData *data(Dialogs::Row *row);

	bool _searching = false;
	QString _lastQuery;
	using ByUsernameRows = QVector<PeerData*>;
	using ByUsernameDatas = QVector<Chat*>;
	ByUsernameRows _byUsername, _byUsernameFiltered;
	ByUsernameDatas d_byUsername, d_byUsernameFiltered; // filtered is partly subset of d_byUsername, partly subset of _byUsernameDatas
	ByUsernameDatas _byUsernameDatas;

};

} // namespace internal
