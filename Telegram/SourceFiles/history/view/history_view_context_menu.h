/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {

enum class PointState : char;
class ListWidget;
class Element;
struct SelectedItem;
using SelectedItems = std::vector<SelectedItem>;

struct ContextMenuRequest {
	explicit ContextMenuRequest(not_null<Main::Session*> session);

	const not_null<Main::Session*> session;
	ClickHandlerPtr link;
	Element *view = nullptr;
	HistoryItem *item = nullptr;
	SelectedItems selectedItems;
	TextForMimeData selectedText;
	bool overSelection = false;
	PointState pointState = PointState();
};

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
	not_null<ListWidget*> list,
	const ContextMenuRequest &request);

void CopyPostLink(FullMsgId itemId);
void StopPoll(FullMsgId itemId);

} // namespace
