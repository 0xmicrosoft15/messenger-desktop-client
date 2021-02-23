/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene_item_base.h"

namespace Data {
class DocumentMedia;
} // namespace Data
namespace Lottie {
class SinglePlayer;
} // namespace Lottie
class DocumentData;

namespace Editor {

class ItemSticker : public ItemBase {
public:
	enum { Type = ItemBase::Type + 1 };

	ItemSticker(
		not_null<DocumentData*> document,
		std::shared_ptr<float64> zPtr,
		int size,
		int x,
		int y);
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
private:
	const not_null<DocumentData*> _document;
	const std::shared_ptr<Data::DocumentMedia> _mediaView;
	const QMarginsF _thumbnailMargins;

	struct {
		std::unique_ptr<Lottie::SinglePlayer> player;
		rpl::lifetime lifetime;
	} _lottie;
	QPixmap _pixmap;

	rpl::lifetime _loadingLifetime;

};

} // namespace Editor
