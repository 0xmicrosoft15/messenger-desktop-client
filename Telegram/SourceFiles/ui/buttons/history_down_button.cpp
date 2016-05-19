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
#include "stdafx.h"
#include "ui/buttons/history_down_button.h"

#include "styles/style_history.h"

namespace Ui {

HistoryDownButton::HistoryDownButton(QWidget *parent) : Button(parent)
, a_arrowOpacity(st::btnAttachEmoji.opacity, st::btnAttachEmoji.opacity)
, _a_arrowOver(animation(this, &HistoryDownButton::step_arrowOver)) {
	setCursor(style::cur_pointer);
	resize(st::historyToDown.width(), st::historyToDown.height());

	connect(this, SIGNAL(stateChanged(int,ButtonStateChangeSource)), this, SLOT(onStateChange(int,ButtonStateChangeSource)));
}

void HistoryDownButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	st::historyToDown.paint(p, QPoint(0, 0), width());
	p.setOpacity(a_arrowOpacity.current());
	st::historyToDownArrow.paint(p, QPoint(0, 0), width());
}

void HistoryDownButton::onStateChange(int oldState, ButtonStateChangeSource source) {
	a_arrowOpacity.start((_state & (StateOver | StateDown)) ? st::btnAttachEmoji.overOpacity : st::btnAttachEmoji.opacity);

	if (source == ButtonByUser || source == ButtonByPress) {
		_a_arrowOver.stop();
		a_arrowOpacity.finish();
		update();
	} else {
		_a_arrowOver.start();
	}
}

void HistoryDownButton::step_arrowOver(float64 ms, bool timer) {
	float64 dt = ms / st::btnAttachEmoji.duration;
	if (dt >= 1) {
		_a_arrowOver.stop();
		a_arrowOpacity.finish();
	} else {
		a_arrowOpacity.update(dt, anim::linear);
	}
	if (timer) update();
}


} // namespace Ui
