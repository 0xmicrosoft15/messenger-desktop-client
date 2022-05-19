/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
struct FileOrigin;
class DocumentMedia;
} // namespace Data

namespace Lottie {
class SinglePlayer;
struct ColorReplacements;
} // namespace Lottie

namespace HistoryView {

class Sticker final
	: public UnwrappedMedia::Content
	, public base::has_weak_ptr {
public:
	Sticker(
		not_null<Element*> parent,
		not_null<DocumentData*> data,
		Element *replacing = nullptr,
		const Lottie::ColorReplacements *replacements = nullptr);
	~Sticker();

	void initSize();
	QSize size() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) override;
	ClickHandlerPtr link() override;

	DocumentData *document() override;
	void stickerClearLoopPlayed() override;
	std::unique_ptr<Lottie::SinglePlayer> stickerTakeLottie(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	//void externalLottieProgressing(bool external) override;
	//bool externalLottieTill(ExternalLottieInfo info) override;
	//ExternalLottieInfo externalLottieInfo() const override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

	void refreshLink() override;

	void setDiceIndex(const QString &emoji, int index);
	[[nodiscard]] bool atTheEnd() const {
		return 	(_frameIndex >= 0) && (_frameIndex + 1 == _framesCount);
	}
	[[nodiscard]] std::optional<int> frameIndex() const {
		return (_frameIndex >= 0)
			? std::make_optional(_frameIndex)
			: std::nullopt;
	}
	[[nodiscard]] std::optional<int> framesCount() const {
		return (_framesCount > 0)
			? std::make_optional(_framesCount)
			: std::nullopt;
	}
	[[nodiscard]] bool readyToDrawLottie();

	[[nodiscard]] static QSize Size();
	[[nodiscard]] static QSize Size(not_null<DocumentData*> document);
	[[nodiscard]] static QSize PremiumEffectSize(
		not_null<DocumentData*> document);
	[[nodiscard]] static QSize EmojiEffectSize();
	[[nodiscard]] static QSize EmojiSize();
	[[nodiscard]] static ClickHandlerPtr ShowSetHandler(
		not_null<DocumentData*> document);

private:
	[[nodiscard]] bool isEmojiSticker() const;
	void paintLottie(Painter &p, const PaintContext &context, const QRect &r);
	bool paintPixmap(Painter &p, const PaintContext &context, const QRect &r);
	void paintPath(Painter &p, const PaintContext &context, const QRect &r);
	[[nodiscard]] QPixmap paintedPixmap(const PaintContext &context) const;
	[[nodiscard]] bool mirrorHorizontal() const;

	void ensureDataMediaCreated() const;
	void dataMediaCreated() const;

	void setupLottie();
	void lottieCreated();
	void unloadLottie();
	void emojiStickerClicked();
	//bool markFramesTillExternal();

	const not_null<Element*> _parent;
	const not_null<DocumentData*> _data;
	const Lottie::ColorReplacements *_replacements = nullptr;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	ClickHandlerPtr _link;
	QSize _size;
	QImage _lastDiceFrame;
	QString _diceEmoji;
	//ExternalLottieInfo _externalInfo;
	int _diceIndex = -1;
	mutable int _frameIndex = -1;
	mutable int _framesCount = -1;
	mutable bool _lottieOncePlayed = false;
	mutable bool _premiumEffectPlayed = false;
	mutable bool _nextLastDiceFrame = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
