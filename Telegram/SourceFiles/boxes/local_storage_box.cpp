/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/local_storage_box.h"

#include "styles/style_boxes.h"
#include "ui/widgets/buttons.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "layout.h"

LocalStorageBox::LocalStorageBox(QWidget *parent)
: _clear(this, lang(lng_local_storage_clear), st::boxLinkButton) {
}

void LocalStorageBox::prepare() {
	setTitle(langFactory(lng_local_storage_title));

	addButton(langFactory(lng_box_ok), [this] { closeBox(); });

	_clear->setClickedCallback([this] { clearStorage(); });

	connect(App::wnd(), SIGNAL(tempDirCleared(int)), this, SLOT(onTempDirCleared(int)));
	connect(App::wnd(), SIGNAL(tempDirClearFailed(int)), this, SLOT(onTempDirClearFailed(int)));

	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });

	updateControls();

	checkLocalStoredCounts();
}

void LocalStorageBox::updateControls() {
	const auto rowsHeight = st::linkFont->height + st::localStorageBoxSkip;
	_clear->setVisible(false);
	setDimensions(st::boxWidth, st::localStorageBoxSkip + rowsHeight + _clear->height());
	_clear->moveToLeft(st::boxPadding.left(), st::localStorageBoxSkip + rowsHeight);
	update();
}

void LocalStorageBox::checkLocalStoredCounts() {
}

void LocalStorageBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::boxTextFont);
	p.setPen(st::windowFg);
	checkLocalStoredCounts();
	auto top = st::localStorageBoxSkip;
	p.drawTextLeft(st::boxPadding.left(), top, width(), lang(lng_settings_no_data_cached));
	top += st::boxTextFont->height + st::localStorageBoxSkip;
	auto text = ([this]() -> QString {
		switch (_state) {
		case State::Clearing: return lang(lng_local_storage_clearing);
		case State::Cleared: return lang(lng_local_storage_cleared);
		case State::ClearFailed: return Lang::Hard::ClearPathFailed();
		}
		return QString();
	})();
	if (!text.isEmpty()) {
		p.drawTextLeft(st::boxPadding.left(), top, width(), text);
		top += st::boxTextFont->height + st::localStorageBoxSkip;
	}
}

void LocalStorageBox::clearStorage() {
	App::wnd()->tempDirDelete(Local::ClearManagerStorage);
	_state = State::Clearing;
	updateControls();
}

void LocalStorageBox::onTempDirCleared(int task) {
	if (task & Local::ClearManagerStorage) {
		_state = State::Cleared;
	}
	updateControls();
}

void LocalStorageBox::onTempDirClearFailed(int task) {
	if (task & Local::ClearManagerStorage) {
		_state = State::ClearFailed;
	}
	updateControls();
}
