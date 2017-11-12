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
#include "window/window_controller.h"

#include "window/main_window.h"
#include "info/info_memento.h"
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
		if (!Auth().data().tabbedSelectorSectionEnabled()
			&& !Auth().data().thirdSectionInfoEnabled()) {
			return true;
		}
		return false;
	};

	if (useOneColumnLayout()) {
		dialogsWidth = chatWidth = bodyWidth;
	} else if (useNormalLayout()) {
		layout = Adaptive::WindowLayout::Normal;
		dialogsWidth = qRound(bodyWidth * Auth().data().dialogsWidthRatio());
		accumulate_max(dialogsWidth, st::columnMinimalWidthLeft);
		accumulate_min(dialogsWidth, bodyWidth - st::columnMinimalWidthMain);
		chatWidth = bodyWidth - dialogsWidth;
	} else {
		layout = Adaptive::WindowLayout::ThreeColumn;
		dialogsWidth = qRound(bodyWidth * Auth().data().dialogsWidthRatio());
		accumulate_max(dialogsWidth, st::columnMinimalWidthLeft);
		thirdWidth = st::columnMinimalWidthThird;
		accumulate_min(
			dialogsWidth,
			bodyWidth - thirdWidth - st::columnMinimalWidthMain);
		chatWidth = bodyWidth - dialogsWidth - thirdWidth;
	}
	return { bodyWidth, dialogsWidth, chatWidth, thirdWidth, layout };
}

bool Controller::canShowThirdSection() const {
	auto currentLayout = computeColumnLayout();
	auto extendBy = minimalThreeColumnWidth()
		- currentLayout.bodyWidth;
	if (extendBy <= 0) {
		return true;
	}
	return window()->canExtendWidthBy(extendBy);
}

bool Controller::canShowThirdSectionWithoutResize() const {
	auto currentWidth = computeColumnLayout().bodyWidth;
	return currentWidth >= minimalThreeColumnWidth();
}

bool Controller::takeThirdSectionFromLayer() {
	return App::wnd()->takeThirdSectionFromLayer();
}

void Controller::resizeForThirdSection() {
	auto layout = computeColumnLayout();
	if (layout.windowLayout == Adaptive::WindowLayout::ThreeColumn) {
		return;
	}

	auto tabbedSelectorSectionEnabled =
		Auth().data().tabbedSelectorSectionEnabled();
	auto thirdSectionInfoEnabled =
		Auth().data().thirdSectionInfoEnabled();
	Auth().data().setTabbedSelectorSectionEnabled(false);
	Auth().data().setThirdSectionInfoEnabled(false);

	auto extendBy = qMax(
		minimalThreeColumnWidth() - layout.bodyWidth,
		st::columnMinimalWidthThird);
	auto newBodyWidth = layout.bodyWidth + extendBy;
	auto currentRatio = Auth().data().dialogsWidthRatio();
	Auth().data().setDialogsWidthRatio(
		(currentRatio * layout.bodyWidth) / newBodyWidth);
	window()->tryToExtendWidthBy(extendBy);

	Auth().data().setTabbedSelectorSectionEnabled(
		tabbedSelectorSectionEnabled);
	Auth().data().setThirdSectionInfoEnabled(
		thirdSectionInfoEnabled);
}

void Controller::closeThirdSection() {
	auto newWindowSize = window()->size();
	auto layout = computeColumnLayout();
	if (layout.windowLayout == Adaptive::WindowLayout::ThreeColumn) {
		auto noResize = window()->isFullScreen()
			|| window()->isMaximized();
		auto newBodyWidth = noResize
			? layout.bodyWidth
			: (layout.bodyWidth - layout.thirdWidth);
		auto currentRatio = Auth().data().dialogsWidthRatio();
		Auth().data().setDialogsWidthRatio((currentRatio * layout.bodyWidth) / newBodyWidth);
		newWindowSize = QSize(
			window()->width() + (newBodyWidth - layout.bodyWidth),
			window()->height());
	}
	Auth().data().setTabbedSelectorSectionEnabled(false);
	Auth().data().setThirdSectionInfoEnabled(false);
	Auth().saveDataDelayed();
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
				return history->scrollTopItem->date.date();
			} else if (history->loadedAtTop() && !history->isEmpty() && history->peer->migrateFrom()) {
				if (auto migrated = App::historyLoaded(history->peer->migrateFrom())) {
					if (migrated->scrollTopItem) {
						// We're up in the migrated history.
						// So current date is the date of first message here.
						return history->blocks.front()->items.front()->date.date();
					}
				}
			} else if (!history->lastMsgDate.isNull()) {
				return history->lastMsgDate.date();
			}
		}
		return QDate::currentDate();
	};
	auto maxPeerDate = [peer] {
		if (auto history = App::historyLoaded(peer)) {
			if (!history->lastMsgDate.isNull()) {
				return history->lastMsgDate.date();
			}
		}
		return QDate::currentDate();
	};
	auto minPeerDate = [peer] {
		if (auto history = App::historyLoaded(peer)) {
			if (history->loadedAtTop()) {
				if (history->isEmpty()) {
					return QDate::currentDate();
				}
				return history->blocks.front()->items.front()->date.date();
			}
		}
		return QDate(2013, 8, 1); // Telegram was launched in August 2013 :)
	};
	auto highlighted = requestedDate.isNull() ? currentPeerDate() : requestedDate;
	auto month = highlighted;
	auto box = Box<CalendarBox>(month, highlighted, [this, peer](const QDate &date) { Auth().api().jumpToDate(peer, date); });
	box->setMinDate(minPeerDate());
	box->setMaxDate(maxPeerDate());
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

void Controller::showPeerInfo(
		PeerId peerId,
		const SectionShow &params) {
	if (Adaptive::ThreeColumn()
		&& !Auth().data().thirdSectionInfoEnabled()) {
		Auth().data().setThirdSectionInfoEnabled(true);
		Auth().saveDataDelayed();
	}
	showSection(Info::Memento(peerId), params);
}

void Controller::showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params) {
	showPeerInfo(peer->id, params);
}

void Controller::showPeerInfo(
		not_null<History*> history,
		const SectionShow &params) {
	showPeerInfo(history->peer->id, params);
}

void Controller::showSection(
		SectionMemento &&memento,
		const SectionShow &params) {
	if (App::wnd()->showSectionInExistingLayer(
			&memento,
			params)) {
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

} // namespace Window
