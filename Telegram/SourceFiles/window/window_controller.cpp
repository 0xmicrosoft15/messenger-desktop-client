/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_controller.h"

#include "window/main_window.h"
#include "info/info_memento.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/history_item.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "boxes/calendar_box.h"
#include "auth_session.h"
#include "apiwrap.h"

namespace Window {

void Controller::enableGifPauseReason(GifPauseReason reason) {
	if (!(_gifPauseReasons & reason)) {
		auto notify = (static_cast<int>(_gifPauseReasons) < static_cast<int>(reason));
		_gifPauseReasons |= reason;
		if (notify) {
			_gifPauseLevelChanged.notify();
		}
	}
}

void Controller::disableGifPauseReason(GifPauseReason reason) {
	if (_gifPauseReasons & reason) {
		_gifPauseReasons &= ~reason;
		if (_gifPauseReasons < reason) {
			_gifPauseLevelChanged.notify();
		}
	}
}

bool Controller::isGifPausedAtLeastFor(GifPauseReason reason) const {
	if (reason == GifPauseReason::Any) {
		return (_gifPauseReasons != 0) || !window()->isActive();
	}
	return (static_cast<int>(_gifPauseReasons) >= 2 * static_cast<int>(reason)) || !window()->isActive();
}

int Controller::dialogsSmallColumnWidth() const {
	return st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x();
}

int Controller::minimalThreeColumnWidth() const {
	return st::columnMinimalWidthLeft
		+ st::columnMinimalWidthMain
		+ st::columnMinimalWidthThird;
}

bool Controller::forceWideDialogs() const {
	if (dialogsListDisplayForced().value()) {
		return true;
	} else if (dialogsListFocused().value()) {
		return true;
	}
	return !App::main()->isMainSectionShown();
}

Controller::ColumnLayout Controller::computeColumnLayout() const {
	auto layout = Adaptive::WindowLayout::OneColumn;

	auto bodyWidth = window()->bodyWidget()->width();
	auto dialogsWidth = 0, chatWidth = 0, thirdWidth = 0;

	auto useOneColumnLayout = [this, bodyWidth] {
		auto minimalNormal = st::columnMinimalWidthLeft
			+ st::columnMinimalWidthMain;
		if (bodyWidth < minimalNormal) {
			return true;
		}
		return false;
	};

	auto useNormalLayout = [this, bodyWidth] {
		// Used if useSmallColumnLayout() == false.
		if (bodyWidth < minimalThreeColumnWidth()) {
			return true;
		}
		if (!Auth().settings().tabbedSelectorSectionEnabled()
			&& !Auth().settings().thirdSectionInfoEnabled()) {
			return true;
		}
		return false;
	};

	if (useOneColumnLayout()) {
		dialogsWidth = chatWidth = bodyWidth;
	} else if (useNormalLayout()) {
		layout = Adaptive::WindowLayout::Normal;
		dialogsWidth = countDialogsWidthFromRatio(bodyWidth);
		accumulate_min(dialogsWidth, bodyWidth - st::columnMinimalWidthMain);
		chatWidth = bodyWidth - dialogsWidth;
	} else {
		layout = Adaptive::WindowLayout::ThreeColumn;
		dialogsWidth = countDialogsWidthFromRatio(bodyWidth);
		thirdWidth = countThirdColumnWidthFromRatio(bodyWidth);
		auto shrink = shrinkDialogsAndThirdColumns(
			dialogsWidth,
			thirdWidth,
			bodyWidth);
		dialogsWidth = shrink.dialogsWidth;
		thirdWidth = shrink.thirdWidth;

		chatWidth = bodyWidth - dialogsWidth - thirdWidth;
	}
	return { bodyWidth, dialogsWidth, chatWidth, thirdWidth, layout };
}

int Controller::countDialogsWidthFromRatio(int bodyWidth) const {
	auto result = qRound(bodyWidth * Auth().settings().dialogsWidthRatio());
	accumulate_max(result, st::columnMinimalWidthLeft);
//	accumulate_min(result, st::columnMaximalWidthLeft);
	return result;
}

int Controller::countThirdColumnWidthFromRatio(int bodyWidth) const {
	auto result = Auth().settings().thirdColumnWidth();
	accumulate_max(result, st::columnMinimalWidthThird);
	accumulate_min(result, st::columnMaximalWidthThird);
	return result;
}

Controller::ShrinkResult Controller::shrinkDialogsAndThirdColumns(
		int dialogsWidth,
		int thirdWidth,
		int bodyWidth) const {
	auto chatWidth = st::columnMinimalWidthMain;
	if (dialogsWidth + thirdWidth + chatWidth <= bodyWidth) {
		return { dialogsWidth, thirdWidth };
	}
	auto thirdWidthNew = ((bodyWidth - chatWidth) * thirdWidth)
		/ (dialogsWidth + thirdWidth);
	auto dialogsWidthNew = ((bodyWidth - chatWidth) * dialogsWidth)
		/ (dialogsWidth + thirdWidth);
	if (thirdWidthNew < st::columnMinimalWidthThird) {
		thirdWidthNew = st::columnMinimalWidthThird;
		dialogsWidthNew = bodyWidth - thirdWidthNew - chatWidth;
		Assert(dialogsWidthNew >= st::columnMinimalWidthLeft);
	} else if (dialogsWidthNew < st::columnMinimalWidthLeft) {
		dialogsWidthNew = st::columnMinimalWidthLeft;
		thirdWidthNew = bodyWidth - dialogsWidthNew - chatWidth;
		Assert(thirdWidthNew >= st::columnMinimalWidthThird);
	}
	return { dialogsWidthNew, thirdWidthNew };
}

bool Controller::canShowThirdSection() const {
	auto currentLayout = computeColumnLayout();
	auto minimalExtendBy = minimalThreeColumnWidth()
		- currentLayout.bodyWidth;
	return (minimalExtendBy <= window()->maximalExtendBy());
}

bool Controller::canShowThirdSectionWithoutResize() const {
	auto currentWidth = computeColumnLayout().bodyWidth;
	return currentWidth >= minimalThreeColumnWidth();
}

bool Controller::takeThirdSectionFromLayer() {
	return App::wnd()->takeThirdSectionFromLayer();
}

void Controller::resizeForThirdSection() {
	if (Adaptive::ThreeColumn()) {
		return;
	}

	auto layout = computeColumnLayout();
	auto tabbedSelectorSectionEnabled =
		Auth().settings().tabbedSelectorSectionEnabled();
	auto thirdSectionInfoEnabled =
		Auth().settings().thirdSectionInfoEnabled();
	Auth().settings().setTabbedSelectorSectionEnabled(false);
	Auth().settings().setThirdSectionInfoEnabled(false);

	auto wanted = countThirdColumnWidthFromRatio(layout.bodyWidth);
	auto minimal = st::columnMinimalWidthThird;
	auto extendBy = wanted;
	auto extendedBy = [&] {
		// Best - extend by third column without moving the window.
		// Next - extend by minimal third column without moving.
		// Next - show third column inside the window without moving.
		// Last - extend with moving.
		if (window()->canExtendNoMove(wanted)) {
			return window()->tryToExtendWidthBy(wanted);
		} else if (window()->canExtendNoMove(minimal)) {
			extendBy = minimal;
			return window()->tryToExtendWidthBy(minimal);
		} else if (layout.bodyWidth >= minimalThreeColumnWidth()) {
			return 0;
		}
		return window()->tryToExtendWidthBy(minimal);
	}();
	if (extendedBy) {
		if (extendBy != Auth().settings().thirdColumnWidth()) {
			Auth().settings().setThirdColumnWidth(extendBy);
		}
		auto newBodyWidth = layout.bodyWidth + extendedBy;
		auto currentRatio = Auth().settings().dialogsWidthRatio();
		Auth().settings().setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
	}
	auto savedValue = (extendedBy == extendBy) ? -1 : extendedBy;
	Auth().settings().setThirdSectionExtendedBy(savedValue);

	Auth().settings().setTabbedSelectorSectionEnabled(
		tabbedSelectorSectionEnabled);
	Auth().settings().setThirdSectionInfoEnabled(
		thirdSectionInfoEnabled);
}

void Controller::closeThirdSection() {
	auto newWindowSize = window()->size();
	auto layout = computeColumnLayout();
	if (layout.windowLayout == Adaptive::WindowLayout::ThreeColumn) {
		auto noResize = window()->isFullScreen()
			|| window()->isMaximized();
		auto savedValue = Auth().settings().thirdSectionExtendedBy();
		auto extendedBy = (savedValue == -1)
			? layout.thirdWidth
			: savedValue;
		auto newBodyWidth = noResize
			? layout.bodyWidth
			: (layout.bodyWidth - extendedBy);
		auto currentRatio = Auth().settings().dialogsWidthRatio();
		Auth().settings().setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
		newWindowSize = QSize(
			window()->width() + (newBodyWidth - layout.bodyWidth),
			window()->height());
	}
	Auth().settings().setTabbedSelectorSectionEnabled(false);
	Auth().settings().setThirdSectionInfoEnabled(false);
	Auth().saveSettingsDelayed();
	if (window()->size() != newWindowSize) {
		window()->resize(newWindowSize);
	} else {
		updateColumnLayout();
	}
}

void Controller::showJumpToDate(not_null<PeerData*> peer, QDate requestedDate) {
	Expects(peer != nullptr);
	auto currentPeerDate = [peer] {
		if (auto history = App::historyLoaded(peer)) {
			if (history->scrollTopItem) {
				return history->scrollTopItem->data()->date.date();
			} else if (history->loadedAtTop() && !history->isEmpty() && history->peer->migrateFrom()) {
				if (auto migrated = App::historyLoaded(history->peer->migrateFrom())) {
					if (migrated->scrollTopItem) {
						// We're up in the migrated history.
						// So current date is the date of first message here.
						return history->blocks.front()->messages.front()->data()->date.date();
					}
				}
			} else if (!history->chatsListDate().isNull()) {
				return history->chatsListDate().date();
			}
		}
		return QDate::currentDate();
	};
	auto maxPeerDate = [](not_null<PeerData*> peer) {
		if (auto channel = peer->migrateTo()) {
			peer = channel;
		}
		if (auto history = App::historyLoaded(peer)) {
			if (!history->chatsListDate().isNull()) {
				return history->chatsListDate().date();
			}
		}
		return QDate::currentDate();
	};
	auto minPeerDate = [](not_null<PeerData*> peer) {
		const auto startDate = [] {
			// Telegram was launched in August 2013 :)
			return QDate(2013, 8, 1);
		};
		if (auto chat = peer->migrateFrom()) {
			if (auto history = App::historyLoaded(chat)) {
				if (history->loadedAtTop()) {
					if (!history->isEmpty()) {
						return history->blocks.front()->messages.front()->data()->date.date();
					}
				} else {
					return startDate();
				}
			}
		}
		if (auto history = App::historyLoaded(peer)) {
			if (history->loadedAtTop()) {
				if (!history->isEmpty()) {
					return history->blocks.front()->messages.front()->data()->date.date();
				}
				return QDate::currentDate();
			}
		}
		return startDate();
	};
	auto highlighted = requestedDate.isNull()
		? currentPeerDate()
		: requestedDate;
	auto month = highlighted;
	auto callback = [this, peer](const QDate &date) {
		Auth().api().jumpToDate(peer, date);
	};
	auto box = Box<CalendarBox>(
		month,
		highlighted,
		std::move(callback));
	box->setMinDate(minPeerDate(peer));
	box->setMaxDate(maxPeerDate(peer));
	Ui::show(std::move(box));
}

void Controller::updateColumnLayout() {
	App::main()->updateColumnLayout();
}

void Controller::showPeerHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId msgId) {
	App::main()->ui_showPeerHistory(
		peerId,
		params,
		msgId);
}

void Controller::showPeerHistory(
		not_null<PeerData*> peer,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(
		peer->id,
		params,
		msgId);
}

void Controller::showPeerHistory(
		not_null<History*> history,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(
		history->peer->id,
		params,
		msgId);
}

void Navigation::showPeerInfo(
		PeerId peerId,
		const SectionShow &params) {
	//if (Adaptive::ThreeColumn()
	//	&& !Auth().settings().thirdSectionInfoEnabled()) {
	//	Auth().settings().setThirdSectionInfoEnabled(true);
	//	Auth().saveSettingsDelayed();
	//}
	showSection(Info::Memento(peerId), params);
}

void Navigation::showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params) {
	showPeerInfo(peer->id, params);
}

void Navigation::showPeerInfo(
		not_null<History*> history,
		const SectionShow &params) {
	showPeerInfo(history->peer->id, params);
}

void Controller::showSection(
		SectionMemento &&memento,
		const SectionShow &params) {
	if (App::wnd()->showSectionInExistingLayer(
			&memento,
			params) && !params.thirdColumn) {
		return;
	}
	App::main()->showSection(std::move(memento), params);
}

void Controller::showBackFromStack(const SectionShow &params) {
	chats()->showBackFromStack(params);
}

void Controller::showSpecialLayer(
		object_ptr<LayerWidget> &&layer,
		anim::type animated) {
	App::wnd()->showSpecialLayer(std::move(layer), animated);
}

not_null<MainWidget*> Controller::chats() const {
	return App::wnd()->chatsWidget();
}

std::unique_ptr<HistoryView::Element> Controller::createMessageView(
		not_null<HistoryMessage*> message,
		HistoryView::Context context) {
	return std::make_unique<HistoryView::Message>(message, context);
}

std::unique_ptr<HistoryView::Element> Controller::createMessageView(
		not_null<HistoryService*> message,
		HistoryView::Context context) {
	return std::make_unique<HistoryView::Service>(message, context);
}

} // namespace Window
