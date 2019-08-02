/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "base/weak_ptr.h"
#include "base/timer.h"

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace HistoryView {

class StickerContent final
	: public UnwrappedMedia::Content
	, public base::has_weak_ptr {
public:
	StickerContent(
		not_null<Element*> parent,
		not_null<DocumentData*> document);
	~StickerContent();

	QSize size() override;
	void draw(Painter &p, const QRect &r, bool selected) override;
	ClickHandlerPtr link() override {
		return _link;
	}

	DocumentData *document() override {
		return _document;
	}
	void clearStickerLoopPlayed() override {
		_lottieOncePlayed = false;
	}
	void unloadHeavyPart() override {
		unloadLottie();
	}
	void refreshLink() override;

private:
	[[nodiscard]] bool isEmojiSticker() const;
	void paintLottie(Painter &p, const QRect &r, bool selected);
	void paintPixmap(Painter &p, const QRect &r, bool selected);
	[[nodiscard]] QPixmap paintedPixmap(bool selected) const;

	void setupLottie();
	void unloadLottie();

	const not_null<Element*> _parent;
	const not_null<DocumentData*> _document;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	ClickHandlerPtr _link;
	QSize _size;
	mutable bool _lottieOncePlayed = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
