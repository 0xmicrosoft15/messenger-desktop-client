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
#include "calls/calls_top_bar.h"

#include "styles/style_calls.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "lang.h"
#include "calls/calls_call.h"
#include "calls/calls_instance.h"

namespace Calls {

TopBar::TopBar(QWidget *parent, const base::weak_unique_ptr<Call> &call) : TWidget(parent)
, _call(call)
, _durationLabel(this, st::callBarLabel)
, _infoLabel(this, st::callBarLabel, lang(lng_call_bar_info).toUpper())
, _hangupLabel(this, st::callBarLabel, lang(lng_call_bar_hangup).toUpper())
, _mute(this, st::callBarMuteToggle)
, _info(this)
, _hangup(this, st::callBarHangup) {
	initControls();
	resize(width(), st::callBarHeight);
}

void TopBar::initControls() {
	_mute->setClickedCallback([this] {
		_call->setMute(!_call->isMute());
	});
	setMuted(_call->isMute());
	subscribe(_call->muteChanged(), [this](bool mute) {
		setMuted(mute);
		update();
	});
	_info->setClickedCallback([this] {
		if (auto call = _call.get()) {
			Current().showInfoPanel(call);
		}
	});
	_hangup->setClickedCallback([this] {
		if (_call) {
			_call->hangup();
		}
	});
	_updateDurationTimer.setCallback([this] { updateDurationText(); });
	updateDurationText();
}

void TopBar::setMuted(bool mute) {
	_mute->setIconOverride(mute ? &st::callBarUnmuteIcon : nullptr);
	_mute->setRippleColorOverride(mute ? &st::callBarUnmuteRipple : nullptr);
	_hangup->setRippleColorOverride(mute ? &st::callBarUnmuteRipple : nullptr);
	_muted = mute;
}

void TopBar::updateDurationText() {
	if (!_call) {
		return;
	}
	auto wasWidth = _durationLabel->width();
	auto durationMs = _call->getDurationMs();
	auto durationSeconds = durationMs / 1000;
	startDurationUpdateTimer(durationMs);
	_durationLabel->setText(formatDurationText(durationSeconds));
	if (_durationLabel->width() != wasWidth) {
		updateControlsGeometry();
	}
}

void TopBar::startDurationUpdateTimer(TimeMs currentDuration) {
	auto msTillNextSecond = 1000 - (currentDuration % 1000);
	_updateDurationTimer.callOnce(msTillNextSecond + 5);
}

void TopBar::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void TopBar::updateControlsGeometry() {
	auto left = 0;
	_mute->moveToLeft(left, 0); left += _mute->width();
	_durationLabel->moveToLeft(left, st::callBarLabelTop); left += _durationLabel->width() + st::callBarSkip;

	auto right = st::callBarRightSkip;
	_hangupLabel->moveToRight(right, st::callBarLabelTop); right += _hangupLabel->width();
	right += st::callBarHangup.width;
	_hangup->setGeometryToRight(0, 0, right, height());
	_info->setGeometryToLeft(_mute->width(), 0, width() - _mute->width() - _hangup->width(), height());

	auto minPadding = qMax(left, right);

	auto infoLeft = (width() - _infoLabel->width()) / 2;
	if (infoLeft < minPadding) {
		infoLeft = left + (width() - left - right - _infoLabel->width()) / 2;
	}
	_infoLabel->moveToLeft(infoLeft, st::callBarLabelTop);
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), _muted ? st::callBarBgMuted : st::callBarBg);
}

TopBar::~TopBar() = default;

} // namespace Calls
