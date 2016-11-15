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
#include "ui/effects/ripple_animation.h"

namespace Ui {

class RippleAnimation::Ripple {
public:
	Ripple(const style::RippleAnimation &st, QPoint origin, int startRadius, const QPixmap &mask, UpdateCallback update);

	void paint(QPainter &p, const QPixmap &mask, uint64 ms);

	void stop();
	bool finished() const {
		return _hiding && !_hide.animating();
	}

private:
	const style::RippleAnimation &_st;
	UpdateCallback _update;

	QPoint _origin;
	int _radiusFrom = 0;
	int _radiusTo = 0;

	bool _hiding = false;
	FloatAnimation _show;
	FloatAnimation _hide;
	QPixmap _cache;
	QImage _frame;

};

RippleAnimation::Ripple::Ripple(const style::RippleAnimation &st, QPoint origin, int startRadius, const QPixmap &mask, UpdateCallback update)
: _st(st)
, _update(update)
, _origin(origin)
, _radiusFrom(startRadius)
, _frame(mask.size(), QImage::Format_ARGB32_Premultiplied) {
	_frame.setDevicePixelRatio(mask.devicePixelRatio());

	QPoint points[] = {
		{ 0, 0 },
		{ _frame.width() / cIntRetinaFactor(), 0 },
		{ _frame.width() / cIntRetinaFactor(), _frame.height() / cIntRetinaFactor() },
		{ 0, _frame.height() / cIntRetinaFactor() },
	};
	for (auto point : points) {
		accumulate_max(_radiusTo, style::point::dotProduct(_origin - point, _origin - point));
	}
	_radiusTo = qRound(sqrt(_radiusTo));

	_show.start(UpdateCallback(_update), 0., 1., _st.showDuration);
}

void RippleAnimation::Ripple::paint(QPainter &p, const QPixmap &mask, uint64 ms) {
	auto opacity = _hide.current(ms, _hiding ? 0. : 1.);
	if (opacity == 0.) {
		return;
	}

	if (_cache.isNull()) {
		auto radius = anim::interpolate(_radiusFrom, _radiusTo, _show.current(ms, 1.));
		_frame.fill(Qt::transparent);
		{
			Painter p(&_frame);
			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.setPen(Qt::NoPen);
			p.setBrush(_st.color);
			p.drawEllipse(_origin, radius, radius);

			p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			p.drawPixmap(0, 0, mask);
		}
		if (radius == _radiusTo) {
			_cache = App::pixmapFromImageInPlace(std_::move(_frame));
		}
	}
	auto saved = p.opacity();
	if (opacity != 1.) p.setOpacity(saved * opacity);
	if (_cache.isNull()) {
		p.drawImage(0, 0, _frame);
	} else {
		p.drawPixmap(0, 0, _cache);
	}
	if (opacity != 1.) p.setOpacity(saved);
}

void RippleAnimation::Ripple::stop() {
	_hiding = true;
	_hide.start(UpdateCallback(_update), 1., 0., _st.hideDuration);
}

RippleAnimation::RippleAnimation(const style::RippleAnimation &st, QImage mask, UpdateCallback callback)
: _st(st)
, _mask(App::pixmapFromImageInPlace(std_::move(mask)))
, _update(std_::move(callback)) {
}


void RippleAnimation::add(QPoint origin, int startRadius) {
	_ripples.push_back(new Ripple(_st, origin, startRadius, _mask, _update));
}

void RippleAnimation::stopLast() {
	if (!_ripples.isEmpty()) {
		_ripples.back()->stop();
	}
}

void RippleAnimation::paint(QPainter &p, int x, int y, int outerWidth, uint64 ms) {
	if (_ripples.isEmpty()) {
		return;
	}

	if (rtl()) x = outerWidth - x - (_mask.width() / cIntRetinaFactor());
	p.translate(x, y);
	for (auto ripple : _ripples) {
		ripple->paint(p, _mask, ms);
	}
	p.translate(-x, -y);
	clearFinished();
}

void RippleAnimation::clearFinished() {
	while (!_ripples.isEmpty() && _ripples.front()->finished()) {
		delete base::take(_ripples.front());
		_ripples.pop_front();
	}
}

void RippleAnimation::clear() {
	for (auto ripple : base::take(_ripples)) {
		delete ripple;
	}
}

} // namespace Ui
