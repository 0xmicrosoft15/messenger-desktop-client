/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder_preview.h"

#include "chat_helpers/stickers_lottie.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "history/view/media/history_view_sticker_player.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/rect.h"

namespace UserpicBuilder {

PreviewPainter::PreviewPainter(int size)
: _size(size)
, _emojiSize(base::SafeRound(_size / M_SQRT2))
, _frameRect(Rect(Size(_size)) - Margins((_size - _emojiSize) / 2)) {
}

not_null<DocumentData*> PreviewPainter::document() const {
	Expects(_media != nullptr);
	return _media->owner();
}

void PreviewPainter::setDocument(
		not_null<DocumentData*> document,
		Fn<void()> updateCallback) {
	if (_media && (document == _media->owner())) {
		return;
	}
	_lifetime.destroy();

	const auto sticker = document->sticker();
	Assert(sticker != nullptr);
	_media = document->createMediaView();
	_media->checkStickerLarge();
	_media->goodThumbnailWanted();

	rpl::single() | rpl::then(
		document->owner().session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (!_media->loaded()) {
			return;
		}
		_lifetime.destroy();
		const auto emojiSize = Size(_emojiSize);
		if (sticker->isLottie()) {
			_player = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
					_media.get(),
					//
					ChatHelpers::StickerLottieSize::EmojiInteractionReserved7,
					emojiSize,
					Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			_player = std::make_unique<HistoryView::WebmPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				emojiSize);
		} else if (sticker) {
			_player = std::make_unique<HistoryView::StaticStickerPlayer>(
				_media->owner()->location(),
				_media->bytes(),
				emojiSize);
		}
		if (_player) {
			_player->setRepaintCallback(updateCallback);
		} else {
			updateCallback();
		}
	}, _lifetime);
}

void PreviewPainter::paintBackground(QPainter &p, const QBrush &brush) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawEllipse(0, 0, _size, _size);
}

bool PreviewPainter::paintForeground(QPainter &p) {
	if (_player && _player->ready()) {
		// resolveIsColored();
		const auto frame = _player->frame(
			Size(_emojiSize),
			(/*_isColored
				? st::profileVerifiedCheckBg->c
				: */QColor(0, 0, 0, 0)),
			false,
			crl::now(),
			_paused);

		if (frame.image.width() == frame.image.height()) {
			p.drawImage(_frameRect, frame.image);
		} else {
			auto frameRect = Rect(frame.image.size().scaled(
				_frameRect.size(),
				Qt::KeepAspectRatio));
			frameRect.moveCenter(_frameRect.center());
			p.drawImage(frameRect, frame.image);
		}
		if (!_paused) {
			_player->markFrameShown();
		}
		return true;
	}
	return false;
}

EmojiUserpic::EmojiUserpic(not_null<Ui::RpWidget*> parent, const QSize &size)
: Ui::RpWidget(parent)
, _painter(size.width())
, _duration(st::slideWrapDuration) {
	resize(size);
}

void EmojiUserpic::setDocument(not_null<DocumentData*> document) {
	_painter.setDocument(document, [=] { update(); });
}

void EmojiUserpic::result(int size, Fn<void(QImage &&image)> done) {
	const auto colors = ranges::views::all(
		_stops
	) | ranges::views::transform([](const QGradientStop &stop) {
		return stop.second;
	}) | ranges::to_vector;
	const auto painter = lifetime().make_state<PreviewPainter>(size);
	// Reset to the first frame.
	painter->setDocument(_painter.document(), [=] {
		auto background = Images::GenerateLinearGradient(Size(size), colors);

		auto p = QPainter(&background);
		while (true) {
			if (painter->paintForeground(p)) {
				break;
			}
		}
		done(std::move(background));
	});
}

void EmojiUserpic::setGradientStops(QGradientStops stops) {
	if (_stops == stops) {
		return;
	}
	if (!_stops.empty()) {
		auto gradient = QLinearGradient(0, 0, width() / 2., height());
		gradient.setStops(base::take(_stops));
		_previousBrush = QBrush(std::move(gradient));
	}
	_stops = std::move(stops);
	{
		auto gradient = QLinearGradient(0, 0, width() / 2., height());
		gradient.setStops(_stops);
		_brush = QBrush(std::move(gradient));
	}
	if (_duration) {
		_animation.stop();
		_animation.start([=] { update(); }, 0., 1., _duration);
	} else {
		update();
	}
}

void EmojiUserpic::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);

	if (_animation.animating() && (_previousBrush != Qt::NoBrush)) {
		_painter.paintBackground(p, _previousBrush);

		p.setOpacity(_animation.value(1.));
	}

	_painter.paintBackground(p, _brush);

	p.setOpacity(1.);
	_painter.paintForeground(p);
}

void EmojiUserpic::setDuration(crl::time duration) {
	_duration = duration;
}

} // namespace UserpicBuilder
