/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_list_widget.h"

#include "history/history_media_types.h"
#include "history/history_message.h"
#include "history/history_item_components.h"
#include "history/view/history_view_context_menu.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "chat_helpers/message_field.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "messenger.h"
#include "apiwrap.h"
#include "layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "auth_session.h"
#include "ui/widgets/popup_menu.h"
#include "core/tl_help.h"
#include "base/overload.h"
#include "lang/lang_keys.h"
#include "boxes/edit_participant_box.h"
#include "data/data_session.h"
#include "data/data_feed.h"
#include "data/data_media_types.h"
#include "styles/style_history.h"

namespace HistoryView {
namespace {

constexpr auto kScrollDateHideTimeout = 1000;
constexpr auto kPreloadedScreensCount = 4;
constexpr auto kPreloadIfLessThanScreens = 2;
constexpr auto kPreloadedScreensCountFull
	= kPreloadedScreensCount + 1 + kPreloadedScreensCount;

} // namespace

template <ListWidget::EnumItemsDirection direction, typename Method>
void ListWidget::enumerateItems(Method method) {
	constexpr auto TopToBottom = (direction == EnumItemsDirection::TopToBottom);

	// No displayed messages in this history.
	if (_items.empty()) {
		return;
	}
	if (_visibleBottom <= _itemsTop || _itemsTop + _itemsHeight <= _visibleTop) {
		return;
	}

	const auto beginning = begin(_items);
	const auto ending = end(_items);
	auto from = TopToBottom
		? std::lower_bound(
			beginning,
			ending,
			_visibleTop,
			[this](auto &elem, int top) {
				return this->itemTop(elem) + elem->height() <= top;
			})
		: std::upper_bound(
			beginning,
			ending,
			_visibleBottom,
			[this](int bottom, auto &elem) {
				return this->itemTop(elem) + elem->height() >= bottom;
			});
	auto wasEnd = (from == ending);
	if (wasEnd) {
		--from;
	}
	if (TopToBottom) {
		Assert(itemTop(from->get()) + from->get()->height() > _visibleTop);
	} else {
		Assert(itemTop(from->get()) < _visibleBottom);
	}

	while (true) {
		auto view = from->get();
		auto itemtop = itemTop(view);
		auto itembottom = itemtop + view->height();

		// Binary search should've skipped all the items that are above / below the visible area.
		if (TopToBottom) {
			Assert(itembottom > _visibleTop);
		} else {
			Assert(itemtop < _visibleBottom);
		}

		if (!method(view, itemtop, itembottom)) {
			return;
		}

		// Skip all the items that are below / above the visible area.
		if (TopToBottom) {
			if (itembottom >= _visibleBottom) {
				return;
			}
		} else {
			if (itemtop <= _visibleTop) {
				return;
			}
		}

		if (TopToBottom) {
			if (++from == ending) {
				break;
			}
		} else {
			if (from == beginning) {
				break;
			}
			--from;
		}
	}
}

template <typename Method>
void ListWidget::enumerateUserpics(Method method) {
	// Find and remember the top of an attached messages pack
	// -1 means we didn't find an attached to next message yet.
	int lowestAttachedItemTop = -1;

	auto userpicCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		// Skip all service messages.
		auto message = view->data()->toHistoryMessage();
		if (!message) return true;

		if (lowestAttachedItemTop < 0 && view->isAttachedToNext()) {
			lowestAttachedItemTop = itemtop + view->marginTop();
		}

		// Call method on a userpic for all messages that have it and for those who are not showing it
		// because of their attachment to the next message if they are bottom-most visible.
		if (view->displayFromPhoto() || (view->hasFromPhoto() && itembottom >= _visibleBottom)) {
			if (lowestAttachedItemTop < 0) {
				lowestAttachedItemTop = itemtop + view->marginTop();
			}
			// Attach userpic to the bottom of the visible area with the same margin as the last message.
			auto userpicMinBottomSkip = st::historyPaddingBottom + st::msgMargin.bottom();
			auto userpicBottom = qMin(itembottom - view->marginBottom(), _visibleBottom - userpicMinBottomSkip);

			// Do not let the userpic go above the attached messages pack top line.
			userpicBottom = qMax(userpicBottom, lowestAttachedItemTop + st::msgPhotoSize);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(view, userpicBottom - st::msgPhotoSize)) {
				return false;
			}
		}

		// Forget the found top of the pack, search for the next one from scratch.
		if (!view->isAttachedToNext()) {
			lowestAttachedItemTop = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::TopToBottom>(userpicCallback);
}

template <typename Method>
void ListWidget::enumerateDates(Method method) {
	// Find and remember the bottom of an single-day messages pack
	// -1 means we didn't find a same-day with previous message yet.
	auto lowestInOneDayItemBottom = -1;

	auto dateCallback = [&](not_null<Element*> view, int itemtop, int itembottom) {
		const auto item = view->data();
		if (lowestInOneDayItemBottom < 0 && view->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = itembottom - view->marginBottom();
		}

		// Call method on a date for all messages that have it and for those who are not showing it
		// because they are in a one day together with the previous message if they are top-most visible.
		if (view->displayDate() || (!item->isEmpty() && itemtop <= _visibleTop)) {
			if (lowestInOneDayItemBottom < 0) {
				lowestInOneDayItemBottom = itembottom - view->marginBottom();
			}
			// Attach date to the top of the visible area with the same margin as it has in service message.
			auto dateTop = qMax(itemtop, _visibleTop) + st::msgServiceMargin.top();

			// Do not let the date go below the single-day messages pack bottom line.
			auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
			dateTop = qMin(dateTop, lowestInOneDayItemBottom - dateHeight);

			// Call the template callback function that was passed
			// and return if it finished everything it needed.
			if (!method(view, itemtop, dateTop)) {
				return false;
			}
		}

		// Forget the found bottom of the pack, search for the next one from scratch.
		if (!view->isInOneDayWithPrevious()) {
			lowestInOneDayItemBottom = -1;
		}

		return true;
	};

	enumerateItems<EnumItemsDirection::BottomToTop>(dateCallback);
}

ListWidget::ListWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<ListDelegate*> delegate)
: RpWidget(parent)
, _delegate(delegate)
, _controller(controller)
, _context(_delegate->listContext())
, _scrollDateCheck([this] { scrollDateCheck(); })
, _selectEnabled(_delegate->listAllowsMultiSelect()) {
	setMouseTracking(true);
	_scrollDateHideTimer.setCallback([this] { scrollDateHideByTimer(); });
	Auth().data().viewRepaintRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			repaintItem(view);
		}
	}, lifetime());
	Auth().data().viewResizeRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			resizeItem(view);
		}
	}, lifetime());
	Auth().data().itemViewRefreshRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto view = viewForItem(item)) {
			refreshItem(view);
		}
	}, lifetime());
	Auth().data().viewLayoutChanged(
	) | rpl::start_with_next([this](auto view) {
		if (view->delegate() == this) {
			if (view->isUnderCursor()) {
				mouseActionUpdate();
			}
		}
	}, lifetime());
	Auth().data().animationPlayInlineRequest(
	) | rpl::start_with_next([this](auto item) {
		if (const auto view = viewForItem(item)) {
			if (const auto media = view->media()) {
				media->playAnimation();
			}
		}
	}, lifetime());
	Auth().data().itemRemoved(
	) | rpl::start_with_next(
		[this](auto item) { itemRemoved(item); },
		lifetime());
	subscribe(Auth().data().queryItemVisibility(), [this](const Data::Session::ItemVisibilityQuery &query) {
		if (const auto view = viewForItem(query.item)) {
			const auto top = itemTop(view);
			if (top >= 0
				&& top + view->height() > _visibleTop
				&& top < _visibleBottom) {
				*query.isVisible = true;
			}
		}
	});
}

not_null<ListDelegate*> ListWidget::delegate() const {
	return _delegate;
}

void ListWidget::refreshViewer() {
	_viewerLifetime.destroy();
	_delegate->listSource(
		_aroundPosition,
		_idsLimit,
		_idsLimit
	) | rpl::start_with_next([=](Data::MessagesSlice &&slice) {
		_slice = std::move(slice);
		refreshRows();
	}, _viewerLifetime);
}

void ListWidget::refreshRows() {
	saveScrollState();

	_items.clear();
	_items.reserve(_slice.ids.size());
	for (const auto &fullId : _slice.ids) {
		if (const auto item = App::histItemById(fullId)) {
			_items.push_back(enforceViewForItem(item));
		}
	}
	updateAroundPositionFromRows();

	updateItemsGeometry();
	restoreScrollState();
	mouseActionUpdate(QCursor::pos());
}

void ListWidget::saveScrollState() {
	if (!_scrollTopState.item) {
		_scrollTopState = countScrollState();
	}
}

void ListWidget::restoreScrollState() {
	if (_items.empty() || !_scrollTopState.item) {
		return;
	}
	const auto index = findNearestItem(_scrollTopState.item);
	if (index >= 0) {
		const auto view = _items[index];
		auto newVisibleTop = itemTop(view) + _scrollTopState.shift;
		if (_visibleTop != newVisibleTop) {
			_delegate->listScrollTo(newVisibleTop);
		}
	}
	_scrollTopState = ScrollTopState();
}

Element *ListWidget::viewForItem(FullMsgId itemId) const {
	if (const auto item = App::histItemById(itemId)) {
		return viewForItem(item);
	}
	return nullptr;
}

Element *ListWidget::viewForItem(const HistoryItem *item) const {
	if (item) {
		if (const auto i = _views.find(item); i != _views.end()) {
			return i->second.get();
		}
	}
	return nullptr;
}

not_null<Element*> ListWidget::enforceViewForItem(
		not_null<HistoryItem*> item) {
	if (const auto view = viewForItem(item)) {
		return view;
	}
	const auto [i, ok] = _views.emplace(
		item,
		item->createView(this));
	return i->second.get();
}

void ListWidget::updateAroundPositionFromRows() {
	_aroundIndex = findNearestItem(_aroundPosition);
	if (_aroundIndex >= 0) {
		_aroundPosition = _items[_aroundIndex]->data()->position();
	}
}

int ListWidget::findNearestItem(Data::MessagePosition position) const {
	if (_items.empty()) {
		return -1;
	}
	const auto after = ranges::find_if(
		_items,
		[&](not_null<Element*> view) {
			return (view->data()->position() >= position);
		});
	return (after == end(_items))
		? int(_items.size() - 1)
		: int(after - begin(_items));
}

void ListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	auto scrolledUp = (visibleTop < _visibleTop);
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	updateVisibleTopItem();
	checkMoveToOtherViewer();
	if (scrolledUp) {
		_scrollDateCheck.call();
	} else {
		scrollDateHideByTimer();
	}
	_controller->floatPlayerAreaUpdated().notify(true);
}

void ListWidget::updateVisibleTopItem() {
	if (_visibleBottom == height()) {
		_visibleTopItem = nullptr;
	} else if (_items.empty()) {
		_visibleTopItem = nullptr;
		_visibleTopFromItem = _visibleTop;
	} else {
		_visibleTopItem = findItemByY(_visibleTop);
		_visibleTopFromItem = _visibleTop - itemTop(_visibleTopItem);
	}
}

bool ListWidget::displayScrollDate() const {
	return (_visibleTop <= height() - 2 * (_visibleBottom - _visibleTop));
}

void ListWidget::scrollDateCheck() {
	if (!_visibleTopItem) {
		_scrollDateLastItem = nullptr;
		_scrollDateLastItemTop = 0;
		scrollDateHide();
	} else if (_visibleTopItem != _scrollDateLastItem || _visibleTopFromItem != _scrollDateLastItemTop) {
		// Show scroll date only if it is not the initial onScroll() event (with empty _scrollDateLastItem).
		if (_scrollDateLastItem && !_scrollDateShown) {
			toggleScrollDateShown();
		}
		_scrollDateLastItem = _visibleTopItem;
		_scrollDateLastItemTop = _visibleTopFromItem;
		_scrollDateHideTimer.callOnce(kScrollDateHideTimeout);
	}
}

void ListWidget::scrollDateHideByTimer() {
	_scrollDateHideTimer.cancel();
	scrollDateHide();
}

void ListWidget::scrollDateHide() {
	if (_scrollDateShown) {
		toggleScrollDateShown();
	}
}

void ListWidget::toggleScrollDateShown() {
	_scrollDateShown = !_scrollDateShown;
	auto from = _scrollDateShown ? 0. : 1.;
	auto to = _scrollDateShown ? 1. : 0.;
	_scrollDateOpacity.start([this] { repaintScrollDateCallback(); }, from, to, st::historyDateFadeDuration);
}

void ListWidget::repaintScrollDateCallback() {
	auto updateTop = _visibleTop;
	auto updateHeight = st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	update(0, updateTop, width(), updateHeight);
}

auto ListWidget::collectSelectedItems() const -> SelectedItems {
	auto transformation = [&](const auto &item) {
		const auto [itemId, selection] = item;
		auto result = SelectedItem(itemId);
		result.canDelete = selection.canDelete;
		result.canForward = selection.canForward;
		return result;
	};
	auto items = SelectedItems();
	if (hasSelectedItems()) {
		items.reserve(_selected.size());
		std::transform(
			_selected.begin(),
			_selected.end(),
			std::back_inserter(items),
			transformation);
	}
	return items;
}

MessageIdsList ListWidget::collectSelectedIds() const {
	const auto selected = collectSelectedItems();
	return ranges::view::all(
		selected
	) | ranges::view::transform([](const SelectedItem &item) {
		return item.msgId;
	}) | ranges::to_vector;
}

void ListWidget::pushSelectedItems() {
	_delegate->listSelectionChanged(collectSelectedItems());
}

void ListWidget::removeItemSelection(
		const SelectedMap::const_iterator &i) {
	Expects(i != _selected.cend());

	_selected.erase(i);
	if (_selected.empty()) {
		update();
	}
	pushSelectedItems();
}

bool ListWidget::hasSelectedText() const {
	return (_selectedTextItem != nullptr) && !hasSelectedItems();
}

bool ListWidget::hasSelectedItems() const {
	return !_selected.empty();
}

bool ListWidget::applyItemSelection(
		SelectedMap &applyTo,
		FullMsgId itemId) const {
	if (applyTo.size() >= MaxSelectedItems) {
		return false;
	}
	auto [iterator, ok] = applyTo.try_emplace(
		itemId,
		SelectionData());
	if (!ok) {
		return false;
	}
	const auto item = App::histItemById(itemId);
	if (!item) {
		applyTo.erase(iterator);
		return false;
	}
	iterator->second.canDelete = item->canDelete();
	iterator->second.canForward = item->allowsForward();
	return true;
}

void ListWidget::toggleItemSelection(FullMsgId itemId) {
	auto it = _selected.find(itemId);
	if (it == _selected.cend()) {
		if (_selectedTextItem) {
			clearTextSelection();
		}
		if (applyItemSelection(_selected, itemId)) {
			repaintItem(itemId);
			pushSelectedItems();
		}
	} else {
		removeItemSelection(it);
	}
}

bool ListWidget::isItemUnderPressSelected() const {
	return itemUnderPressSelection() != _selected.end();
}

auto ListWidget::itemUnderPressSelection() -> SelectedMap::iterator {
	return (_pressState.itemId && _pressState.inside)
		? _selected.find(_pressState.itemId)
		: _selected.end();
}

auto ListWidget::itemUnderPressSelection() const
-> SelectedMap::const_iterator {
	return (_pressState.itemId && _pressState.inside)
		? _selected.find(_pressState.itemId)
		: _selected.end();
}

bool ListWidget::requiredToStartDragging(
		not_null<Element*> view) const {
	if (_mouseCursorState == HistoryInDateCursorState) {
		return true;
	} else if (const auto media = view->media()) {
		return media->type() == MediaTypeSticker;
	}
	return false;
}

bool ListWidget::isPressInSelectedText(HistoryTextState state) const {
	if (state.cursor != HistoryInTextCursorState) {
		return false;
	}
	if (!hasSelectedText()
		|| !_selectedTextItem
		|| _selectedTextItem->fullId() != _pressState.itemId) {
		return false;
	}
	auto from = _selectedTextRange.from;
	auto to = _selectedTextRange.to;
	return (state.symbol >= from && state.symbol < to);
}

void ListWidget::cancelSelection() {
	clearSelected();
	clearTextSelection();
}

void ListWidget::clearSelected() {
	if (_selected.empty()) {
		return;
	}
	if (hasSelectedText()) {
		repaintItem(_selected.begin()->first);
		_selected.clear();
	} else {
		_selected.clear();
		pushSelectedItems();
		update();
	}
}

void ListWidget::clearTextSelection() {
	if (_selectedTextItem) {
		if (const auto view = viewForItem(_selectedTextItem)) {
			repaintItem(view);
		}
		_selectedTextItem = nullptr;
		_selectedTextRange = TextSelection();
		_selectedText = TextWithEntities();
	}
}

void ListWidget::setTextSelection(
		not_null<Element*> view,
		TextSelection selection) {
	clearSelected();
	const auto item = view->data();
	if (_selectedTextItem != item) {
		clearTextSelection();
		_selectedTextItem = view->data();
	}
	_selectedTextRange = selection;
	_selectedText = (selection.from != selection.to)
		? view->selectedText(selection)
		: TextWithEntities();
	repaintItem(view);
	if (!_wasSelectedText && !_selectedText.text.isEmpty()) {
		_wasSelectedText = true;
		setFocus();
	}
}

void ListWidget::checkMoveToOtherViewer() {
	auto visibleHeight = (_visibleBottom - _visibleTop);
	if (width() <= 0
		|| visibleHeight <= 0
		|| _items.empty()
		|| _aroundIndex < 0
		|| _scrollTopState.item) {
		return;
	}

	auto topItem = findItemByY(_visibleTop);
	auto bottomItem = findItemByY(_visibleBottom);
	auto preloadedHeight = kPreloadedScreensCountFull * visibleHeight;
	auto minItemHeight = st::msgMarginTopAttached
		+ st::msgPhotoSize
		+ st::msgMargin.bottom();
	auto preloadedCount = preloadedHeight / minItemHeight;
	auto preloadIdsLimitMin = (preloadedCount / 2) + 1;
	auto preloadIdsLimit = preloadIdsLimitMin
		+ (visibleHeight / minItemHeight);

	auto preloadBefore = kPreloadIfLessThanScreens * visibleHeight;
	auto after = _slice.skippedAfter;
	auto preloadTop = (_visibleTop < preloadBefore);
	auto topLoaded = after && (*after == 0);
	auto before = _slice.skippedBefore;
	auto preloadBottom = (height() - _visibleBottom < preloadBefore);
	auto bottomLoaded = before && (*before == 0);

	auto minScreenDelta = kPreloadedScreensCount
		- kPreloadIfLessThanScreens;
	auto minUniversalIdDelta = (minScreenDelta * visibleHeight)
		/ minItemHeight;
	auto preloadAroundMessage = [&](not_null<Element*> view) {
		auto preloadRequired = false;
		auto itemPosition = view->data()->position();
		auto itemIndex = ranges::find(_items, view) - begin(_items);
		Assert(itemIndex < _items.size());

		if (!preloadRequired) {
			preloadRequired = (_idsLimit < preloadIdsLimitMin);
		}
		if (!preloadRequired) {
			Assert(_aroundIndex >= 0);
			auto delta = std::abs(itemIndex - _aroundIndex);
			preloadRequired = (delta >= minUniversalIdDelta);
		}
		if (preloadRequired) {
			_idsLimit = preloadIdsLimit;
			_aroundPosition = itemPosition;
			_aroundIndex = itemIndex;
			refreshViewer();
		}
	};

	if (preloadTop && !topLoaded) {
		preloadAroundMessage(topItem);
	} else if (preloadBottom && !bottomLoaded) {
		preloadAroundMessage(bottomItem);
	}
}

QString ListWidget::tooltipText() const {
	const auto item = (_overItem && _mouseAction == MouseAction::None)
		? _overItem->data().get()
		: nullptr;
	if (_mouseCursorState == HistoryInDateCursorState && item) {
		return item->date.toString(
			QLocale::system().dateTimeFormat(QLocale::LongFormat));
	} else if (_mouseCursorState == HistoryInForwardedCursorState && item) {
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			return forwarded->text.originalText(
				AllTextSelection,
				ExpandLinksNone);
		}
	} else if (const auto link = ClickHandler::getActive()) {
		return link->tooltip();
	}
	return QString();
}

QPoint ListWidget::tooltipPos() const {
	return _mousePosition;
}

Context ListWidget::elementContext() {
	return _delegate->listContext();
}

std::unique_ptr<Element> ListWidget::elementCreate(
		not_null<HistoryMessage*> message) {
	return std::make_unique<Message>(this, message);
}

std::unique_ptr<Element> ListWidget::elementCreate(
		not_null<HistoryService*> message) {
	return std::make_unique<Service>(this, message);
}

bool ListWidget::elementUnderCursor(
		not_null<const HistoryView::Element*> view) {
	return (_overItem == view);
}

void ListWidget::elementAnimationAutoplayAsync(
		not_null<const Element*> view) {
	crl::on_main(this, [this, msgId = view->data()->fullId()]{
		if (const auto view = viewForItem(msgId)) {
			if (const auto media = view->media()) {
				media->autoplayAnimation();
			}
		}
	});
}

void ListWidget::saveState(not_null<ListMemento*> memento) {
	memento->setAroundPosition(_aroundPosition);
	auto state = countScrollState();
	if (state.item) {
		memento->setIdsLimit(_idsLimit);
		memento->setScrollTopState(state);
	}
}

void ListWidget::restoreState(not_null<ListMemento*> memento) {
	_aroundPosition = memento->aroundPosition();
	_aroundIndex = -1;
	if (const auto limit = memento->idsLimit()) {
		_idsLimit = limit;
		_scrollTopState = memento->scrollTopState();
	}
	refreshViewer();
}

void ListWidget::updateItemsGeometry() {
	const auto count = int(_items.size());
	const auto first = [&] {
		for (auto i = 0; i != count; ++i) {
			const auto view = _items[i].get();
			if (view->isHiddenByGroup()) {
				view->setDisplayDate(false);
			} else {
				view->setDisplayDate(true);
				return i;
			}
		}
		return count;
	}();
	refreshAttachmentsFromTill(first, count);
}

void ListWidget::updateSize() {
	TWidget::resizeToWidth(width());
	restoreScrollPosition();
	updateVisibleTopItem();
}

int ListWidget::resizeGetHeight(int newWidth) {
	update();

	const auto resizeAllItems = (_itemsWidth != newWidth);
	auto newHeight = 0;
	for (auto &view : _items) {
		view->setY(newHeight);
		if (view->pendingResize() || resizeAllItems) {
			newHeight += view->resizeGetHeight(newWidth);
		} else {
			newHeight += view->height();
		}
	}
	_itemsWidth = newWidth;
	_itemsHeight = newHeight;
	_itemsTop = (_minHeight > _itemsHeight + st::historyPaddingBottom) ? (_minHeight - _itemsHeight - st::historyPaddingBottom) : 0;
	return _itemsTop + _itemsHeight + st::historyPaddingBottom;
}

void ListWidget::restoreScrollPosition() {
	auto newVisibleTop = _visibleTopItem
		? (itemTop(_visibleTopItem) + _visibleTopFromItem)
		: ScrollMax;
	_delegate->listScrollTo(newVisibleTop);
}

TextSelection ListWidget::computeRenderSelection(
		not_null<const SelectedMap*> selected,
		not_null<const Element*> view) const {
	const auto itemSelection = [&](not_null<HistoryItem*> item) {
		auto i = selected->find(item->fullId());
		if (i != selected->end()) {
			return FullSelection;
		}
		return TextSelection();
	};
	const auto item = view->data();
	if (const auto group = Auth().data().groups().find(item)) {
		if (group->items.back() != item) {
			return TextSelection();
		}
		auto result = TextSelection();
		auto allFullSelected = true;
		const auto count = int(group->items.size());
		for (auto i = 0; i != count; ++i) {
			if (itemSelection(group->items[i]) == FullSelection) {
				result = AddGroupItemSelection(result, i);
			} else {
				allFullSelected = false;
			}
		}
		if (allFullSelected) {
			return FullSelection;
		}
		const auto leaderSelection = itemSelection(item);
		if (leaderSelection != FullSelection
			&& leaderSelection != TextSelection()) {
			return leaderSelection;
		}
		return result;
	}
	return itemSelection(item);
}

TextSelection ListWidget::itemRenderSelection(
		not_null<const Element*> view) const {
	if (_dragSelectAction != DragSelectAction::None) {
		const auto i = _dragSelected.find(view->data()->fullId());
		if (i != _dragSelected.end()) {
			return (_dragSelectAction == DragSelectAction::Selecting)
				? FullSelection
				: TextSelection();
		}
	}
	if (!_selected.empty() || !_dragSelected.empty()) {
		return computeRenderSelection(&_selected, view);
	} else if (view->data() == _selectedTextItem) {
		return _selectedTextRange;
	}
	return TextSelection();
}

void ListWidget::paintEvent(QPaintEvent *e) {
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}

	Painter p(this);

	auto ms = getms();
	auto clip = e->rect();

	auto from = std::lower_bound(begin(_items), end(_items), clip.top(), [this](auto &elem, int top) {
		return this->itemTop(elem) + elem->height() <= top;
	});
	auto to = std::lower_bound(begin(_items), end(_items), clip.top() + clip.height(), [this](auto &elem, int bottom) {
		return this->itemTop(elem) < bottom;
	});
	if (from != end(_items)) {
		auto top = itemTop(from->get());
		p.translate(0, top);
		for (auto i = from; i != to; ++i) {
			const auto view = *i;
			view->draw(
				p,
				clip.translated(0, -top),
				itemRenderSelection(view),
				ms);
			const auto height = view->height();
			top += height;
			p.translate(0, height);
		}
		p.translate(0, -top);

		enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
			// stop the enumeration if the userpic is below the painted rect
			if (userpicTop >= clip.top() + clip.height()) {
				return false;
			}

			// paint the userpic if it intersects the painted rect
			if (userpicTop + st::msgPhotoSize > clip.top()) {
				const auto message = view->data()->toHistoryMessage();
				Assert(message != nullptr);

				message->from()->paintUserpicLeft(
					p,
					st::historyPhotoLeft,
					userpicTop,
					view->width(),
					st::msgPhotoSize);
			}
			return true;
		});

		auto dateHeight = st::msgServicePadding.bottom() + st::msgServiceFont->height + st::msgServicePadding.top();
		auto scrollDateOpacity = _scrollDateOpacity.current(ms, _scrollDateShown ? 1. : 0.);
		enumerateDates([&](not_null<Element*> view, int itemtop, int dateTop) {
			// stop the enumeration if the date is above the painted rect
			if (dateTop + dateHeight <= clip.top()) {
				return false;
			}

			const auto displayDate = view->displayDate();
			auto dateInPlace = displayDate;
			if (dateInPlace) {
				const auto correctDateTop = itemtop + st::msgServiceMargin.top();
				dateInPlace = (dateTop < correctDateTop + dateHeight);
			}
			//bool noFloatingDate = (item->date.date() == lastDate && displayDate);
			//if (noFloatingDate) {
			//	if (itemtop < showFloatingBefore) {
			//		noFloatingDate = false;
			//	}
			//}

			// paint the date if it intersects the painted rect
			if (dateTop < clip.top() + clip.height()) {
				auto opacity = (dateInPlace/* || noFloatingDate*/) ? 1. : scrollDateOpacity;
				if (opacity > 0.) {
					p.setOpacity(opacity);
					int dateY = /*noFloatingDate ? itemtop :*/ (dateTop - st::msgServiceMargin.top());
					int width = view->width();
					if (const auto date = view->Get<HistoryView::DateBadge>()) {
						date->paint(p, dateY, width);
					} else {
						ServiceMessagePainter::paintDate(
							p,
							view->data()->date,
							dateY,
							width);
					}
				}
			}
			return true;
		});
	}
}

void ListWidget::applyDragSelection() {
	applyDragSelection(_selected);
	clearDragSelection();
	pushSelectedItems();
}

void ListWidget::applyDragSelection(SelectedMap &applyTo) const {
	if (_dragSelectAction == DragSelectAction::Selecting) {
		for (const auto itemId : _dragSelected) {
			applyItemSelection(applyTo, itemId);
		}
	} else if (_dragSelectAction == DragSelectAction::Deselecting) {
		for (const auto itemId : _dragSelected) {
			applyTo.remove(itemId);
		}
	}
}

TextWithEntities ListWidget::getSelectedText() const {
	auto selected = _selected;

	if (_mouseAction == MouseAction::Selecting && !_dragSelected.empty()) {
		applyDragSelection(selected);
	}

	if (selected.empty()) {
		if (const auto view = viewForItem(_selectedTextItem)) {
			return view->selectedText(_selectedTextRange);
		}
		return _selectedText;
	}

	// #TODO selection
	//const auto timeFormat = qsl(", [dd.MM.yy hh:mm]\n");
	//auto groupLeadersAdded = base::flat_set<not_null<Element*>>();
	//auto fullSize = 0;
	//auto texts = base::flat_map<std::pair<int, MsgId>, TextWithEntities>();

	//const auto addItem = [&](
	//		not_null<HistoryView::Element*> view,
	//		TextSelection selection) {
	//	const auto item = view->data();
	//	auto time = item->date.toString(timeFormat);
	//	auto part = TextWithEntities();
	//	auto unwrapped = view->selectedText(selection);
	//	auto size = item->author()->name.size()
	//		+ time.size()
	//		+ unwrapped.text.size();
	//	part.text.reserve(size);

	//	auto y = itemTop(view);
	//	if (y >= 0) {
	//		part.text.append(item->author()->name).append(time);
	//		TextUtilities::Append(part, std::move(unwrapped));
	//		texts.emplace(std::make_pair(y, item->id), part);
	//		fullSize += size;
	//	}
	//};

	//for (const auto [item, selection] : selected) {
	//	const auto view = item->mainView();
	//	if (!view) {
	//		continue;
	//	}

	//	if (const auto group = Auth().data().groups().find(item)) {
	//		// #TODO group copy
	//		//if (groupLeadersAdded.contains(group->leader)) {
	//		//	continue;
	//		//}
	//		//const auto leaderSelection = computeRenderSelection(
	//		//	&selected,
	//		//	group->leader);
	//		//if (leaderSelection == FullSelection) {
	//		//	groupLeadersAdded.emplace(group->leader);
	//		//	addItem(group->leader, FullSelection);
	//		//} else if (view == group->leader) {
	//		//	const auto leaderFullSelection = AddGroupItemSelection(
	//		//		TextSelection(),
	//		//		int(group->others.size()));
	//		//	addItem(view, leaderFullSelection);
	//		//} else {
	//		//	addItem(view, FullSelection);
	//		//}
	//	} else {
	//		addItem(view, FullSelection);
	//	}
	//}

	auto result = TextWithEntities();
	//auto sep = qsl("\n\n");
	//result.text.reserve(fullSize + (texts.size() - 1) * sep.size());
	//for (auto i = texts.begin(), e = texts.end(); i != e;) {
	//	TextUtilities::Append(result, std::move(i->second));
	//	if (++i != e) {
	//		result.text.append(sep);
	//	}
	//}
	return result;
}

MessageIdsList ListWidget::getSelectedItems() const {
	return collectSelectedIds();
}

not_null<Element*> ListWidget::findItemByY(int y) const {
	Expects(!_items.empty());

	if (y < _itemsTop) {
		return _items.front();
	}
	auto i = std::lower_bound(
		begin(_items),
		end(_items),
		y,
		[this](auto &elem, int top) {
			return this->itemTop(elem) + elem->height() <= top;
		});
	return (i != end(_items)) ? i->get() : _items.back().get();
}

Element *ListWidget::strictFindItemByY(int y) const {
	if (_items.empty()) {
		return nullptr;
	}
	return (y >= _itemsTop && y < _itemsTop + _itemsHeight)
		? findItemByY(y).get()
		: nullptr;
}

auto ListWidget::countScrollState() const -> ScrollTopState {
	if (_items.empty()) {
		return { Data::MessagePosition(), 0 };
	}
	auto topItem = findItemByY(_visibleTop);
	return {
		topItem->data()->position(),
		_visibleTop - itemTop(topItem)
	};
}

void ListWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		_delegate->listCloseRequest();
	} else if (e == QKeySequence::Copy
		&& (hasSelectedText() || hasSelectedItems())) {
		SetClipboardWithEntities(getSelectedText());
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E
		&& e->modifiers().testFlag(Qt::ControlModifier)) {
		SetClipboardWithEntities(getSelectedText(), QClipboard::FindBuffer);
#endif // Q_OS_MAC
	} else {
		e->ignore();
	}
}

void ListWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());
	trySwitchToWordSelection();
}

void ListWidget::trySwitchToWordSelection() {
	auto selectingSome = (_mouseAction == MouseAction::Selecting)
		&& hasSelectedText();
	auto willSelectSome = (_mouseAction == MouseAction::None)
		&& !hasSelectedItems();
	auto checkSwitchToWordSelection = _overItem
		&& (_mouseSelectType == TextSelectType::Letters)
		&& (selectingSome || willSelectSome);
	if (checkSwitchToWordSelection) {
		switchToWordSelection();
	}
}

void ListWidget::switchToWordSelection() {
	Expects(_overItem != nullptr);

	HistoryStateRequest request;
	request.flags |= Text::StateRequest::Flag::LookupSymbol;
	auto dragState = _overItem->getState(_pressState.cursor, request);
	if (dragState.cursor != HistoryInTextCursorState) {
		return;
	}
	_mouseTextSymbol = dragState.symbol;
	_mouseSelectType = TextSelectType::Words;
	if (_mouseAction == MouseAction::None) {
		_mouseAction = MouseAction::Selecting;
		setTextSelection(_overItem, TextSelection(
			dragState.symbol,
			dragState.symbol
		));
	}
	mouseActionUpdate();

	_trippleClickPoint = _mousePosition;
	_trippleClickStartTime = getms();
}

void ListWidget::validateTrippleClickStartTime() {
	if (_trippleClickStartTime) {
		const auto elapsed = (getms() - _trippleClickStartTime);
		if (elapsed >= QApplication::doubleClickInterval()) {
			_trippleClickStartTime = 0;
		}
	}
}

void ListWidget::contextMenuEvent(QContextMenuEvent *e) {
	showContextMenu(e);
}

void ListWidget::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (e->reason() == QContextMenuEvent::Mouse) {
		mouseActionUpdate(e->globalPos());
	}

	ContextMenuRequest request;
	request.link = ClickHandler::getActive();
	request.view = _overItem;
	request.overView = _overItem && _overState.inside;
	request.selectedText = _selectedText;
	const auto itemId = request.view
		? request.view->data()->fullId()
		: FullMsgId();
	if (!_selected.empty()) {
		request.selectedItems = collectSelectedIds();
		if (request.overView && _selected.find(itemId) != end(_selected)) {
			request.overSelection = true;
		}
	} else if (_selectedTextItem
		&& request.view
		&& _selectedTextItem == request.view->data()
		&& request.overView) {
		const auto pointInItem = mapPointToItem(
			mapFromGlobal(_mousePosition),
			request.view);
		HistoryStateRequest stateRequest;
		stateRequest.flags |= Text::StateRequest::Flag::LookupSymbol;
		const auto dragState = request.view->getState(
			pointInItem,
			stateRequest);
		if (dragState.cursor == HistoryInTextCursorState
			&& base::in_range(
				dragState.symbol,
				_selectedTextRange.from,
				_selectedTextRange.to)) {
			request.overSelection = true;
		}
	}
	if (showFromTouch) {
		request.overSelection = true;
	}

	_menu = FillContextMenu(this, request);
	if (_menu && !_menu->actions().isEmpty()) {
		_menu->popup(e->globalPos());
		e->accept();
	} else if (_menu) {
		_menu = nullptr;
	}
}

void ListWidget::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	mouseActionStart(e->globalPos(), e->button());
}

void ListWidget::mouseMoveEvent(QMouseEvent *e) {
	auto buttonsPressed = (e->buttons() & (Qt::LeftButton | Qt::MiddleButton));
	if (!buttonsPressed && _mouseAction != MouseAction::None) {
		mouseReleaseEvent(e);
	}
	mouseActionUpdate(e->globalPos());
}

void ListWidget::mouseReleaseEvent(QMouseEvent *e) {
	mouseActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void ListWidget::enterEventHook(QEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return TWidget::enterEventHook(e);
}

void ListWidget::leaveEventHook(QEvent *e) {
	if (const auto view = _overItem) {
		if (_overState.inside) {
			repaintItem(view);
			_overState.inside = false;
		}
	}
	ClickHandler::clearActive();
	Ui::Tooltip::Hide();
	if (!ClickHandler::getPressed() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	return TWidget::leaveEventHook(e);
}

void ListWidget::updateDragSelection() {
	if (!_overState.itemId || !_pressState.itemId) {
		clearDragSelection();
		return;
	} else if (_items.empty() || !_overItem || !_selectEnabled) {
		return;
	}
	const auto pressItem = App::histItemById(_pressState.itemId);
	if (!pressItem) {
		return;
	}

	const auto overView = _overItem;
	const auto pressView = viewForItem(pressItem);
	const auto selectingUp = _delegate->listIsLessInOrder(
		overView->data(),
		pressItem);
	const auto fromView = selectingUp ? overView : pressView;
	const auto tillView = selectingUp ? pressView : overView;
	// #TODO skip-from / skip-till
	const auto from = [&] {
		if (fromView) {
			const auto result = ranges::find(
				_items,
				fromView,
				[](auto view) { return view.get(); });
			return (result == end(_items)) ? begin(_items) : result;
		}
		return begin(_items);
	}();
	const auto till = tillView
		? ranges::find(
			_items,
			tillView,
			[](auto view) { return view.get(); })
		: end(_items);
	Assert(from <= till);
	for (auto i = begin(_items); i != from; ++i) {
		_dragSelected.remove((*i)->data()->fullId());
	}
	for (auto i = from; i != till; ++i) {
		_dragSelected.emplace((*i)->data()->fullId());
	}
	for (auto i = till; i != end(_items); ++i) {
		_dragSelected.remove((*i)->data()->fullId());
	}
	_dragSelectAction = [&] {
		if (_dragSelected.empty()) {
			return DragSelectAction::None;
		} else if (!pressView) {
			return _dragSelectAction;
		}
		if (_selected.find(pressItem->fullId()) != end(_selected)) {
			return DragSelectAction::Deselecting;
		} else {
			return DragSelectAction::Selecting;
		}
	}();
	if (!_wasSelectedText
		&& !_dragSelected.empty()
		&& _dragSelectAction == DragSelectAction::Selecting) {
		_wasSelectedText = true;
		setFocus();
	}
	update();
}

void ListWidget::clearDragSelection() {
	_dragSelectAction = DragSelectAction::None;
	if (!_dragSelected.empty()) {
		_dragSelected.clear();
		update();
	}
}

void ListWidget::mouseActionStart(
		const QPoint &globalPosition,
		Qt::MouseButton button) {
	mouseActionUpdate(globalPosition);
	if (button != Qt::LeftButton) {
		return;
	}

	ClickHandler::pressed();
	if (_pressState != _overState) {
		if (_pressState.itemId != _overState.itemId) {
			repaintItem(_pressState.itemId);
		}
		_pressState = _overState;
		repaintItem(_overState.itemId);
	}
	const auto pressedItem = _overItem;

	_mouseAction = MouseAction::None;
	_pressWasInactive = _controller->window()->wasInactivePress();
	if (_pressWasInactive) _controller->window()->setInactivePress(false);

	if (ClickHandler::getPressed()) {
		_mouseAction = MouseAction::PrepareDrag;
	}
	if (_mouseAction == MouseAction::None && pressedItem) {
		validateTrippleClickStartTime();
		HistoryTextState dragState;
		auto startDistance = (globalPosition - _trippleClickPoint).manhattanLength();
		auto validStartPoint = startDistance < QApplication::startDragDistance();
		if (_trippleClickStartTime != 0 && validStartPoint) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = pressedItem->getState(_pressState.cursor, request);
			if (dragState.cursor == HistoryInTextCursorState) {
				setTextSelection(pressedItem, TextSelection(
					dragState.symbol,
					dragState.symbol
				));
				_mouseTextSymbol = dragState.symbol;
				_mouseAction = MouseAction::Selecting;
				_mouseSelectType = TextSelectType::Paragraphs;
				mouseActionUpdate();
				_trippleClickStartTime = getms();
			}
		} else if (pressedItem) {
			HistoryStateRequest request;
			request.flags = Text::StateRequest::Flag::LookupSymbol;
			dragState = pressedItem->getState(_pressState.cursor, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			_mouseTextSymbol = dragState.symbol;
			if (isPressInSelectedText(dragState)) {
				_mouseAction = MouseAction::PrepareDrag; // start text drag
			} else if (!_pressWasInactive) {
				if (requiredToStartDragging(pressedItem)) {
					_mouseAction = MouseAction::PrepareDrag;
				} else {
					if (dragState.afterSymbol) ++_mouseTextSymbol;
					;
					if (!hasSelectedItems()) {
						setTextSelection(pressedItem, TextSelection(
							_mouseTextSymbol,
							_mouseTextSymbol));
						_mouseAction = MouseAction::Selecting;
					} else {
						_mouseAction = MouseAction::PrepareSelect;
					}
				}
			}
		}
	}
	if (!pressedItem) {
		_mouseAction = MouseAction::None;
	} else if (_mouseAction == MouseAction::None) {
		mouseActionCancel();
	}
}

void ListWidget::mouseActionUpdate(const QPoint &globalPosition) {
	_mousePosition = globalPosition;
	mouseActionUpdate();
}

void ListWidget::mouseActionCancel() {
	_pressState = CursorState();
	_mouseAction = MouseAction::None;
	clearDragSelection();
	_wasSelectedText = false;
	//_widget->noSelectingScroll(); // #TODO select scroll
}

void ListWidget::mouseActionFinish(
		const QPoint &globalPosition,
		Qt::MouseButton button) {
	mouseActionUpdate(globalPosition);

	auto activated = ClickHandler::unpressed();
	if (_mouseAction == MouseAction::Dragging) {
		activated = nullptr;
	}
	auto pressState = base::take(_pressState);
	repaintItem(pressState.itemId);

	auto simpleSelectionChange = pressState.itemId
		&& pressState.inside
		&& !_pressWasInactive
		&& (button != Qt::RightButton)
		&& (_mouseAction == MouseAction::PrepareDrag
			|| _mouseAction == MouseAction::PrepareSelect);
	auto needItemSelectionToggle = simpleSelectionChange
		&& hasSelectedItems();
	auto needTextSelectionClear = simpleSelectionChange
		&& hasSelectedText();

	_wasSelectedText = false;

	if (activated) {
		mouseActionCancel();
		App::activateClickHandler(activated, button);
		return;
	}
	if (needItemSelectionToggle) {
		toggleItemSelection(pressState.itemId);
	} else if (needTextSelectionClear) {
		clearTextSelection();
	} else if (_mouseAction == MouseAction::Selecting) {
		if (!_dragSelected.empty()) {
			applyDragSelection();
		} else if (_selectedTextItem && !_pressWasInactive) {
			if (_selectedTextRange.from == _selectedTextRange.to) {
				clearTextSelection();
				App::wnd()->setInnerFocus();
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseSelectType = TextSelectType::Letters;
	//_widget->noSelectingScroll(); // #TODO select scroll

#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (_selectedTextItem
		&& _selectedTextRange.from != _selectedTextRange.to) {
		if (const auto view = viewForItem(_selectedTextItem)) {
			SetClipboardWithEntities(
				_selectedTextItem->selectedText(_selectedTextRange),
				QClipboard::Selection);
}
	}
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void ListWidget::mouseActionUpdate() {
	auto mousePosition = mapFromGlobal(_mousePosition);
	auto point = QPoint(snap(mousePosition.x(), 0, width()), snap(mousePosition.y(), _visibleTop, _visibleBottom));

	const auto view = strictFindItemByY(point.y());
	const auto item = view ? view->data().get() : nullptr;
	const auto itemPoint = mapPointToItem(point, view);
	_overState = CursorState{
		item ? item->fullId() : FullMsgId(),
		view ? view->height() : 0,
		itemPoint,
		view ? view->hasPoint(itemPoint) : false
	};
	if (_overItem != view) {
		repaintItem(_overItem);
		_overItem = view;
		repaintItem(_overItem);
	}

	HistoryTextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto inTextSelection = _overState.inside
		&& (_overState.itemId == _pressState.itemId)
		&& hasSelectedText();
	if (view) {
		auto cursorDeltaLength = [&] {
			auto cursorDelta = (_overState.cursor - _pressState.cursor);
			return cursorDelta.manhattanLength();
		};
		auto dragStartLength = [] {
			return QApplication::startDragDistance();
		};
		if (_overState.itemId != _pressState.itemId
			|| cursorDeltaLength() >= dragStartLength()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				InvokeQueued(this, [this] { performDrag(); });
			} else if (_mouseAction == MouseAction::PrepareSelect) {
				_mouseAction = MouseAction::Selecting;
			}
		}
		HistoryStateRequest request;
		if (_mouseAction == MouseAction::Selecting) {
			request.flags |= Text::StateRequest::Flag::LookupSymbol;
		} else {
			inTextSelection = false;
		}
		// #TODO enumerate dates like HistoryInner
		dragState = view->getState(itemPoint, request);
		lnkhost = view;
		if (!dragState.link
			&& itemPoint.x() >= st::historyPhotoLeft
			&& itemPoint.x() < st::historyPhotoLeft + st::msgPhotoSize) {
			if (view->hasFromPhoto()) {
				enumerateUserpics([&](not_null<Element*> view, int userpicTop) {
					// stop enumeration if the userpic is below our point
					if (userpicTop > point.y()) {
						return false;
					}

					// stop enumeration if we've found a userpic under the cursor
					if (point.y() >= userpicTop && point.y() < userpicTop + st::msgPhotoSize) {
						const auto message = view->data()->toHistoryMessage();
						Assert(message != nullptr);

						dragState.link = message->from()->openLink();
						lnkhost = view;
						return false;
					}
					return true;
				});
			}
		}
	}
	auto lnkChanged = ClickHandler::setActive(dragState.link, lnkhost);
	if (lnkChanged || dragState.cursor != _mouseCursorState) {
		Ui::Tooltip::Hide();
	}
	if (dragState.link
		|| dragState.cursor == HistoryInDateCursorState
		|| dragState.cursor == HistoryInForwardedCursorState) {
		Ui::Tooltip::Show(1000, this);
	}

	auto cursor = style::cur_default;
	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.cursor;
		auto cursor = computeMouseCursor();
		if (_cursor != cursor) {
			setCursor((_cursor = cursor));
		}
	} else if (view) {
		if (_mouseAction == MouseAction::Selecting) {
			if (inTextSelection) {
				auto second = dragState.symbol;
				if (dragState.afterSymbol
					&& _mouseSelectType == TextSelectType::Letters) {
					++second;
				}
				auto selection = TextSelection(
					qMin(second, _mouseTextSymbol),
					qMax(second, _mouseTextSymbol)
				);
				if (_mouseSelectType != TextSelectType::Letters) {
					selection = view->adjustSelection(
						selection,
						_mouseSelectType);
				}
				setTextSelection(view, selection);
				clearDragSelection();
			} else if (_pressState.itemId) {
				updateDragSelection();
			}
		} else if (_mouseAction == MouseAction::Dragging) {
		}
	}

	// Voice message seek support.
	if (_pressState.inside && ClickHandler::getPressed()) {
		if (const auto item = App::histItemById(_pressState.itemId)) {
			if (const auto view = viewForItem(item)) {
				auto adjustedPoint = mapPointToItem(point, view);
				view->updatePressed(adjustedPoint);
			}
		}
	}

	//if (_mouseAction == MouseAction::Selecting) {
	//	_widget->checkSelectingScroll(mousePos);
	//} else {
	//	_widget->noSelectingScroll();
	//} // #TODO select scroll
}

style::cursor ListWidget::computeMouseCursor() const {
	if (ClickHandler::getPressed() || ClickHandler::getActive()) {
		return style::cur_pointer;
	} else if (!hasSelectedItems()
		&& (_mouseCursorState == HistoryInTextCursorState)) {
		return style::cur_text;
	}
	return style::cur_default;
}

void ListWidget::performDrag() {
	if (_mouseAction != MouseAction::Dragging) return;

	auto uponSelected = false;
	//if (_mouseActionItem) {
	//	if (!_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
	//		uponSelected = _selected.contains(_mouseActionItem);
	//	} else {
	//		HistoryStateRequest request;
	//		request.flags |= Text::StateRequest::Flag::LookupSymbol;
	//		auto dragState = _mouseActionItem->getState(_dragStartPosition.x(), _dragStartPosition.y(), request);
	//		uponSelected = (dragState.cursor == HistoryInTextCursorState);
	//		if (uponSelected) {
	//			if (_selected.isEmpty() ||
	//				_selected.cbegin().value() == FullSelection ||
	//				_selected.cbegin().key() != _mouseActionItem
	//				) {
	//				uponSelected = false;
	//			} else {
	//				uint16 selFrom = _selected.cbegin().value().from, selTo = _selected.cbegin().value().to;
	//				if (dragState.symbol < selFrom || dragState.symbol >= selTo) {
	//					uponSelected = false;
	//				}
	//			}
	//		}
	//	}
	//}
	//auto pressedHandler = ClickHandler::getPressed();

	//if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.data())) {
	//	return;
	//}

	//TextWithEntities sel;
	//QList<QUrl> urls;
	//if (uponSelected) {
	//	sel = getSelectedText();
	//} else if (pressedHandler) {
	//	sel = { pressedHandler->dragText(), EntitiesInText() };
	//	//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
	//	//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
	//	//}
	//}
	//if (auto mimeData = mimeDataFromTextWithEntities(sel)) {
	//	updateDragSelection(0, 0, false);
	//	_widget->noSelectingScroll();

	//	if (!urls.isEmpty()) mimeData->setUrls(urls);
	//	if (uponSelected && !Adaptive::OneColumn()) {
	//		auto selectedState = getSelectionState();
	//		if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
	//			Auth().data().setMimeForwardIds(getSelectedItems());
	//			mimeData->setData(qsl("application/x-td-forward"), "1");
	//		}
	//	}
	//	_controller->window()->launchDrag(std::move(mimeData));
	//	return;
	//} else {
	//	auto forwardMimeType = QString();
	//	auto pressedMedia = static_cast<HistoryMedia*>(nullptr);
	//	if (auto pressedItem = App::pressedItem()) { // #TODO no App::
	//		pressedMedia = pressedItem->media();
	//		if (_mouseCursorState == HistoryInDateCursorState
	//			|| (pressedMedia && pressedMedia->dragItem())) {
	//			Auth().data().setMimeForwardIds(
	//				Auth().data().itemOrItsGroup(pressedItem->data()));
	//			forwardMimeType = qsl("application/x-td-forward");
	//		}
	//	}
	//	if (auto pressedLnkItem = App::pressedLinkItem()) { // #TODO no App::
	//		if ((pressedMedia = pressedLnkItem->media())) {
	//			if (forwardMimeType.isEmpty()
	//				&& pressedMedia->dragItemByHandler(pressedHandler)) {
	//				Auth().data().setMimeForwardIds(
	//					{ 1, pressedLnkItem->fullId() });
	//				forwardMimeType = qsl("application/x-td-forward");
	//			}
	//		}
	//	}
	//	if (!forwardMimeType.isEmpty()) {
	//		auto mimeData = std::make_unique<QMimeData>();
	//		mimeData->setData(forwardMimeType, "1");
	//		if (auto document = (pressedMedia ? pressedMedia->getDocument() : nullptr)) {
	//			auto filepath = document->filepath(DocumentData::FilePathResolveChecked);
	//			if (!filepath.isEmpty()) {
	//				QList<QUrl> urls;
	//				urls.push_back(QUrl::fromLocalFile(filepath));
	//				mimeData->setUrls(urls);
	//			}
	//		}

	//		// This call enters event loop and can destroy any QObject.
	//		_controller->window()->launchDrag(std::move(mimeData));
	//		return;
	//	}
	//} // #TODO drag
}

int ListWidget::itemTop(not_null<const Element*> view) const {
	return _itemsTop + view->y();
}

void ListWidget::repaintItem(const Element *view) {
	if (!view) {
		return;
	}
	update(0, itemTop(view), width(), view->height());
}

void ListWidget::repaintItem(FullMsgId itemId) {
	if (const auto view = viewForItem(itemId)) {
		repaintItem(view);
	}
}

void ListWidget::resizeItem(not_null<Element*> view) {
	const auto index = ranges::find(_items, view) - begin(_items);
	if (index < int(_items.size())) {
		refreshAttachmentsAtIndex(index);
	}
}

void ListWidget::refreshAttachmentsAtIndex(int index) {
	Expects(index >= 0 && index < _items.size());

	const auto from = [&] {
		if (index > 0) {
			for (auto i = index - 1; i != 0; --i) {
				if (!_items[i]->isHiddenByGroup()) {
					return i;
				}
			}
		}
		return index;
	}();
	const auto till = [&] {
		for (auto i = index + 1, count = int(_items.size()); i != count; ) {
			if (!_items[i]->isHiddenByGroup()) {
				return i + 1;
			}
		}
		return index + 1;
	}();
	refreshAttachmentsFromTill(from, till);
}

void ListWidget::refreshAttachmentsFromTill(int from, int till) {
	Expects(from >= 0 && from <= till && till <= int(_items.size()));

	if (from == till) {
		return;
	}
	auto view = _items[from].get();
	for (auto i = from + 1; i != till; ++i) {
		const auto next = _items[i].get();
		if (next->isHiddenByGroup()) {
			next->setDisplayDate(false);
		} else {
			const auto viewDate = view->data()->date;
			const auto nextDate = next->data()->date;
			next->setDisplayDate(nextDate.date() != viewDate.date());
			auto attached = next->computeIsAttachToPrevious(view);
			next->setAttachToPrevious(attached);
			view->setAttachToNext(attached);
			view = next;
		}
	}
	updateSize();
}

void ListWidget::refreshItem(not_null<const Element*> view) {
	const auto i = ranges::find(_items, view);
	const auto index = i - begin(_items);
	if (index < int(_items.size())) {
		const auto item = view->data();
		_views.erase(item);
		const auto [i, ok] = _views.emplace(
			item,
			item->createView(this));
		const auto was = view;
		const auto now = i->second.get();
		_items[index] = now;

		viewReplaced(view, i->second.get());

		refreshAttachmentsAtIndex(index);
	}
}

void ListWidget::viewReplaced(not_null<const Element*> was, Element *now) {
	if (_visibleTopItem == was) _visibleTopItem = now;
	if (_scrollDateLastItem == was) _scrollDateLastItem = now;
	if (_overItem == was) _overItem = now;
}

void ListWidget::itemRemoved(not_null<const HistoryItem*> item) {
	if (_selectedTextItem == item) {
		clearTextSelection();
	}
	const auto i = _views.find(item);
	if (i == end(_views)) {
		return;
	}
	const auto view = i->second.get();
	_items.erase(
		ranges::remove(_items, view, [](auto view) { return view.get(); }),
		end(_items));
	viewReplaced(view, nullptr);
	_views.erase(i);

	updateItemsGeometry();
}

QPoint ListWidget::mapPointToItem(
		QPoint point,
		const Element *view) const {
	if (!view) {
		return QPoint();
	}
	return point - QPoint(0, itemTop(view));
}

ListWidget::~ListWidget() = default;

} // namespace HistoryView
