/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_voice_record_bar.h"

#include "api/api_send_progress.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "styles/style_chat.h"
#include "ui/controls/send_button.h"
#include "ui/text/format_values.h"
#include "window/window_session_controller.h"

namespace HistoryView::Controls {

namespace {

using SendActionUpdate = VoiceRecordBar::SendActionUpdate;
using VoiceToSend = VoiceRecordBar::VoiceToSend;

constexpr auto kRecordingUpdateDelta = crl::time(100);
constexpr auto kAudioVoiceMaxLength = 100 * 60; // 100 minutes
constexpr auto kMaxSamples =
	::Media::Player::kDefaultFrequency * kAudioVoiceMaxLength;

constexpr auto kPrecision = 10;

[[nodiscard]] auto Duration(int samples) {
	return samples / ::Media::Player::kDefaultFrequency;
}

[[nodiscard]] auto FormatVoiceDuration(int samples) {
	const int duration = kPrecision
		* (float64(samples) / ::Media::Player::kDefaultFrequency);
	const auto durationString = Ui::FormatDurationText(duration / kPrecision);
	const auto decimalPart = duration % kPrecision;
	return QString("%1%2%3")
		.arg(durationString)
		.arg(QLocale::system().decimalPoint())
		.arg(decimalPart);
}

} // namespace

VoiceRecordBar::VoiceRecordBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	std::shared_ptr<Ui::SendButton> send,
	int recorderHeight)
: RpWidget(parent)
, _controller(controller)
, _wrap(std::make_unique<Ui::RpWidget>(parent))
, _send(send)
, _cancelFont(st::historyRecordFont)
, _recordingAnimation([=](crl::time now) {
	return recordingAnimationCallback(now);
}) {
	resize(QSize(parent->width(), recorderHeight));
	init();
}

VoiceRecordBar::~VoiceRecordBar() {
	if (isRecording()) {
		stopRecording(false);
	}
}

void VoiceRecordBar::updateControlsGeometry(QSize size) {
	_centerY = size.height() / 2;
	{
		const auto maxD = st::historyRecordSignalMax * 2;
		const auto point = _centerY - st::historyRecordSignalMax;
		_redCircleRect = { point, point, maxD, maxD };
	}
	{
		const auto durationLeft = _redCircleRect.x()
			+ _redCircleRect.width()
			+ st::historyRecordDurationSkip;
		_durationRect = QRect(
			durationLeft,
			_redCircleRect.y(),
			_cancelFont->width(FormatVoiceDuration(kMaxSamples)),
			_redCircleRect.height());
	}
	{
		const auto left = _durationRect.x()
			+ _durationRect.width()
			+ ((_send->width() - st::historyRecordVoice.width()) / 2);
		const auto right = width() - _send->width();
		const auto width = _cancelFont->width(tr::lng_record_cancel(tr::now));
		_messageRect = QRect(
			left + (right - left - width) / 2,
			st::historyRecordTextTop,
			width + st::historyRecordDurationSkip,
			_cancelFont->height);
	}
}

void VoiceRecordBar::init() {
	hide();
	// Keep VoiceRecordBar behind SendButton.
	rpl::single(
	) | rpl::then(
		_send->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::ZOrderChange;
		}) | rpl::to_empty
	) | rpl::start_with_next([=] {
		stackUnder(_send.get());
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry(size);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);
		if (_showAnimation.animating()) {
			p.setOpacity(_showAnimation.value(1.));
		}
		p.fillRect(clip, st::historyComposeAreaBg);

		if (clip.intersects(_messageRect)) {
			// The message should be painted first to avoid flickering.
			drawMessage(p, activeAnimationRatio());
		}
		if (clip.intersects(_redCircleRect)) {
			drawRecording(p);
		}
		if (clip.intersects(_durationRect)) {
			drawDuration(p);
		}
	}, lifetime());

	_inField.changes(
	) | rpl::start_with_next([=](bool value) {
		activeAnimate(value);
	}, lifetime());
}

void VoiceRecordBar::activeAnimate(bool active) {
	const auto to = active ? 1. : 0.;
	const auto duration = st::historyRecordVoiceDuration;
	if (_activeAnimation.animating()) {
		_activeAnimation.change(to, duration);
	} else {
		auto callback = [=] {
			update(_messageRect);
			_send->requestPaintRecord(activeAnimationRatio());
		};
		const auto from = active ? 0. : 1.;
		_activeAnimation.start(std::move(callback), from, to, duration);
	}
}

void VoiceRecordBar::visibilityAnimate(bool show, Fn<void()> &&callback) {
	const auto to = show ? 1. : 0.;
	const auto from = show ? 0. : 1.;
	const auto duration = st::historyRecordVoiceShowDuration;
	auto animationCallback = [=, callback = std::move(callback)](auto value) {
		update();
		if ((show && value == 1.) || (!show && value == 0.)) {
			if (callback) {
				callback();
			}
		}
	};
	_showAnimation.start(std::move(animationCallback), from, to, duration);
}

void VoiceRecordBar::startRecording() {
	auto appearanceCallback = [=] {
		Expects(!_showAnimation.animating());

		using namespace ::Media::Capture;
		if (!instance()->available()) {
			stop(false);
			return;
		}

		_recording = true;
		instance()->start();
		instance()->updated(
		) | rpl::start_with_next_error([=](const Update &update) {
			recordUpdated(update.level, update.samples);
		}, [=] {
			stop(false);
		}, _recordingLifetime);
	};
	visibilityAnimate(true, std::move(appearanceCallback));
	show();

	_inField = true;
	_controller->widget()->setInnerFocus();

	_send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord()
			&& (e->type() == QEvent::MouseMove
				|| e->type() == QEvent::MouseButtonRelease);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			_inField = rect().contains(mapFromGlobal(mouse->globalPos()));
		} else if (type == QEvent::MouseButtonRelease) {
			stop(_inField.current());
		}
	}, _recordingLifetime);
}

bool VoiceRecordBar::recordingAnimationCallback(crl::time now) {
	const auto dt = anim::Disabled()
		? 1.
		: ((now - _recordingAnimation.started())
			/ float64(kRecordingUpdateDelta));
	if (dt >= 1.) {
		_recordingLevel.finish();
	} else {
		_recordingLevel.update(dt, anim::linear);
	}
	if (!anim::Disabled()) {
		update(_redCircleRect);
	}
	return (dt < 1.);
}

void VoiceRecordBar::recordUpdated(quint16 level, int samples) {
	_recordingLevel.start(level);
	_recordingAnimation.start();
	_recordingSamples = samples;
	if (samples < 0 || samples >= kMaxSamples) {
		stop(samples > 0 && _inField.current());
	}
	Core::App().updateNonIdle();
	update(_durationRect);
	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice });
}

void VoiceRecordBar::stop(bool send) {
	auto disappearanceCallback = [=] {
		Expects(!_showAnimation.animating());

		hide();
		_recording = false;

		stopRecording(send);

		_recordingLevel = anim::value();
		_recordingAnimation.stop();

		_inField = false;

		_recordingLifetime.destroy();
		_recordingSamples = 0;
		_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice, -1 });

		_controller->widget()->setInnerFocus();
	};
	visibilityAnimate(false, std::move(disappearanceCallback));
}

void VoiceRecordBar::stopRecording(bool send) {
	using namespace ::Media::Capture;
	if (!send) {
		instance()->stop();
		return;
	}
	instance()->stop(crl::guard(this, [=](const Result &data) {
		if (data.bytes.isEmpty()) {
			return;
		}

		Window::ActivateWindow(_controller);
		const auto duration = Duration(data.samples);
		_sendVoiceRequests.fire({ data.bytes, data.waveform, duration });
	}));
}

void VoiceRecordBar::drawDuration(Painter &p) {
	const auto duration = FormatVoiceDuration(_recordingSamples);
	p.setFont(_cancelFont);
	p.setPen(st::historyRecordDurationFg);

	p.drawText(_durationRect, style::al_left, duration);
}

void VoiceRecordBar::drawRecording(Painter &p) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordSignalColor);

	const auto min = st::historyRecordSignalMin;
	const auto max = st::historyRecordSignalMax;
	const auto delta = std::min(_recordingLevel.current() / 0x4000, 1.);
	const auto radii = qRound(min + (delta * (max - min)));
	const auto center = _redCircleRect.center() + QPoint(1, 1);
	p.drawEllipse(center, radii, radii);
}

void VoiceRecordBar::drawMessage(Painter &p, float64 recordActive) {
	p.setPen(
		anim::pen(
			st::historyRecordCancel,
			st::historyRecordCancelActive,
			1. - recordActive));
	p.drawText(
		_messageRect.x(),
		_messageRect.y() + _cancelFont->ascent,
		tr::lng_record_cancel(tr::now));
}

rpl::producer<SendActionUpdate> VoiceRecordBar::sendActionUpdates() const {
	return _sendActionUpdates.events();
}

rpl::producer<VoiceToSend> VoiceRecordBar::sendVoiceRequests() const {
	return _sendVoiceRequests.events();
}

bool VoiceRecordBar::isRecording() const {
	return _recording.current();
}

void VoiceRecordBar::finishAnimating() {
	_recordingAnimation.stop();
	_showAnimation.stop();
}

rpl::producer<bool> VoiceRecordBar::recordingStateChanges() const {
	return _recording.changes();
}

rpl::producer<> VoiceRecordBar::startRecordingRequests() const {
	return _send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord() && (e->type() == QEvent::MouseButtonPress);
	}) | rpl::to_empty;
}

bool VoiceRecordBar::isTypeRecord() const {
	return (_send->type() == Ui::SendButton::Type::Record);
}

float64 VoiceRecordBar::activeAnimationRatio() const {
	return _activeAnimation.value(_inField.current() ? 1. : 0.);
}

} // namespace HistoryView::Controls
