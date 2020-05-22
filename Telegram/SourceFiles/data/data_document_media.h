/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

class FileLoader;

namespace Data {

class DocumentMedia final {
public:
	explicit DocumentMedia(not_null<DocumentData*> owner);
	~DocumentMedia();

	[[nodiscard]] not_null<DocumentData*> owner() const;

	void goodThumbnailWanted();
	[[nodiscard]] Image *goodThumbnail() const; // #TODO optimize QImage-wrap
	void setGoodThumbnail(QImage thumbnail);

	[[nodiscard]] Image *thumbnailInline() const;

	[[nodiscard]] Image *thumbnail() const;
	[[nodiscard]] QSize thumbnailSize() const;
	void thumbnailWanted(Data::FileOrigin origin);
	void setThumbnail(QImage thumbnail);

	[[nodiscard]] QByteArray videoThumbnailContent() const;
	[[nodiscard]] QSize videoThumbnailSize() const;
	void videoThumbnailWanted(Data::FileOrigin origin);
	void setVideoThumbnail(QByteArray content);

	void checkStickerLarge();
	void checkStickerSmall();
	[[nodiscard]] Image *getStickerSmall();
	[[nodiscard]] Image *getStickerLarge();
	void checkStickerLarge(not_null<FileLoader*> loader);

	void setBytes(const QByteArray &bytes);
	[[nodiscard]] QByteArray bytes() const;
	[[nodiscard]] bool loaded(bool check = false) const;
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] bool canBePlayed() const;

	void automaticLoad(Data::FileOrigin origin, const HistoryItem *item);

	// For DocumentData.
	static void CheckGoodThumbnail(not_null<DocumentData*> document);

private:
	enum class Flag : uchar {
		GoodThumbnailWanted = 0x01,
	};
	inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	static void ReadOrGenerateThumbnail(not_null<DocumentData*> document);
	static void GenerateGoodThumbnail(
		not_null<DocumentData*> document,
		QByteArray data);

	[[nodiscard]] bool thumbnailEnoughForSticker() const;

	const not_null<DocumentData*> _owner;
	std::unique_ptr<Image> _goodThumbnail;
	mutable std::unique_ptr<Image> _inlineThumbnail;
	std::unique_ptr<Image> _thumbnail;
	std::unique_ptr<Image> _sticker;
	QByteArray _bytes;
	QByteArray _videoThumbnailBytes;
	Flags _flags;

};

} // namespace Data
