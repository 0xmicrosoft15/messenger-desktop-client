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
#include "media/view/media_clip_playback.h"

#include "styles/style_mediaview.h"
#include "media/media_audio.h"

namespace Media {
namespace Clip {

Playback::Playback(QWidget *parent) : TWidget(parent)
, _a_progress(animation(this, &Playback::step_progress)) {
	setCursor(style::cur_pointer);
}

void Playback::updateState(const AudioPlaybackState &playbackState) {
	qint64 position = 0, duration = playbackState.duration;

	if (!(playbackState.state & AudioPlayerStoppedMask) && playbackState.state != AudioPlayerFinishing) {
		position = playbackState.position;
	} else if (playbackState.state == AudioPlayerStoppedAtEnd) {
		position = playbackState.duration;
	} else {
		position = 0;
	}

	float64 progress = 0.;
	if (duration) {
		progress = duration ? snap(float64(position) / duration, 0., 1.) : 0.;
	}
	if (duration != _duration || position != _position) {
		if (duration && _duration) {
			a_progress.start(progress);
			_a_progress.start();
		} else {
			a_progress = anim::fvalue(progress, progress);
			_a_progress.stop();
		}
		_position = position;
		_duration = duration;
	}
}

void Playback::step_progress(float64 ms, bool timer) {
	float64 dt = ms / (2 * AudioVoiceMsgUpdateView);
	if (_duration && dt >= 1) {
		_a_progress.stop();
		a_progress.finish();
	} else {
		a_progress.update(qMin(dt, 1.), anim::linear);
	}
	if (timer) update();
}

void Playback::paintEvent(QPaintEvent *e) {
	Painter p(this);

	int radius = st::mediaviewPlaybackWidth / 2;
	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::HighQualityAntialiasing);

	auto over = _a_over.current(getms(), _over ? 1. : 0.);
	int skip = (st::mediaviewSeekSize.width() / 2);
	int length = (width() - st::mediaviewSeekSize.width());
	float64 prg = _mouseDown ? _downProgress : a_progress.current();
	int32 from = skip, mid = qRound(from + prg * length), end = from + length;
	if (mid > from) {
		p.setClipRect(0, 0, mid, height());
		p.setOpacity(over * st::mediaviewActiveOpacity + (1. - over) * st::mediaviewInactiveOpacity);
		p.setBrush(st::mediaviewPlaybackActive);
		p.drawRoundedRect(0, (height() - st::mediaviewPlaybackWidth) / 2, mid + radius, st::mediaviewPlaybackWidth, radius, radius);
	}
	if (end > mid) {
		p.setClipRect(mid, 0, width() - mid, height());
		p.setOpacity(1.);
		p.setBrush(st::mediaviewPlaybackInactive);
		p.drawRoundedRect(mid - radius, (height() - st::mediaviewPlaybackWidth) / 2, width() - (mid - radius), st::mediaviewPlaybackWidth, radius, radius);
	}
	int x = mid - skip;
	p.setClipRect(rect());
	p.setOpacity(over * st::mediaviewActiveOpacity + (1. - over) * st::mediaviewInactiveOpacity);
	p.setBrush(st::mediaviewPlaybackActive);
	p.drawRoundedRect(x, (height() - st::mediaviewSeekSize.height()) / 2, st::mediaviewSeekSize.width(), st::mediaviewSeekSize.height(), st::mediaviewSeekSize.width() / 2, st::mediaviewSeekSize.width() / 2);
}

void Playback::mouseMoveEvent(QMouseEvent *e) {
}

void Playback::mousePressEvent(QMouseEvent *e) {
}

void Playback::mouseReleaseEvent(QMouseEvent *e) {
}

void Playback::enterEvent(QEvent *e) {
	setOver(true);
}

void Playback::leaveEvent(QEvent *e) {
	setOver(false);
}

void Playback::setOver(bool over) {
	if (_over == over) return;

	_over = over;
	auto from = _over ? 0. : 1., to = _over ? 1. : 0.;
	START_ANIMATION(_a_over, func(this, &Playback::updateCallback), from, to, st::mediaviewOverDuration, anim::linear);
}

} // namespace Clip
} // namespace Media
