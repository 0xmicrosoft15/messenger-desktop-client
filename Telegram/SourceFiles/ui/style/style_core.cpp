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

namespace style {
namespace internal {
namespace {

using ModulesList = QList<internal::ModuleBase*>;
NeverFreedPointer<ModulesList> styleModules;

void startModules() {
	if (!styleModules) return;

	for_const (auto module, *styleModules) {
		module->start();
	}
}

void stopModules() {
	if (!styleModules) return;

	for_const (auto module, *styleModules) {
		module->stop();
	}
}

} // namespace

void registerModule(ModuleBase *module) {
	styleModules.createIfNull();
	styleModules->push_back(module);
}

void unregisterModule(ModuleBase *module) {
	styleModules->removeOne(module);
	if (styleModules->isEmpty()) {
		styleModules.clear();
	}
}

} // namespace internal

void startManager() {
	if (cRetina()) {
		cSetRealScale(dbisOne);
	}

	internal::registerFontFamily(qsl("Open Sans"));
	internal::startModules();
}

void stopManager() {
	internal::stopModules();
	internal::destroyFonts();
	internal::destroyColors();
	internal::destroyIcons();
}

void colorizeImage(const QImage &src, QColor c, QImage *outResult, QRect srcRect, QPoint dstPoint) {
	if (srcRect.isNull()) {
		srcRect = src.rect();
	} else {
		t_assert(src.rect().contains(srcRect));
	}
	auto width = srcRect.width();
	auto height = srcRect.height();
	t_assert(outResult && outResult->rect().contains(QRect(dstPoint, srcRect.size())));

	auto initialAlpha = c.alpha() + 1;
	auto red = (c.red() * initialAlpha) >> 8;
	auto green = (c.green() * initialAlpha) >> 8;
	auto blue = (c.blue() * initialAlpha) >> 8;
	auto alpha = (255 * initialAlpha) >> 8;
	auto pattern = static_cast<uint64>(alpha)
		| (static_cast<uint64>(red) << 16)
		| (static_cast<uint64>(green) << 32)
		| (static_cast<uint64>(blue) << 48);

	auto resultBytesPerPixel = (src.depth() >> 3);
	auto resultIntsPerPixel = 1;
	auto resultIntsPerLine = (outResult->bytesPerLine() >> 2);
	auto resultIntsAdded = resultIntsPerLine - width * resultIntsPerPixel;
	auto resultInts = reinterpret_cast<uint32*>(outResult->bits()) + dstPoint.y() * resultIntsPerLine + dstPoint.x() * resultIntsPerPixel;
	t_assert(resultIntsAdded >= 0);
	t_assert(outResult->depth() == ((resultIntsPerPixel * sizeof(uint32)) << 3));
	t_assert(outResult->bytesPerLine() == (resultIntsPerLine << 2));

	auto maskBytesPerPixel = (src.depth() >> 3);
	auto maskBytesPerLine = src.bytesPerLine();
	auto maskBytesAdded = maskBytesPerLine - width * maskBytesPerPixel;
	auto maskBytes = src.constBits() + srcRect.y() * maskBytesPerLine + srcRect.x() * maskBytesPerPixel;
	t_assert(maskBytesAdded >= 0);
	t_assert(src.depth() == (maskBytesPerPixel << 3));
	for (int y = 0; y != height; ++y) {
		for (int x = 0; x != width; ++x) {
			auto maskOpacity = static_cast<uint64>(*maskBytes) + 1;
			auto masked = (pattern * maskOpacity) >> 8;
			auto alpha = static_cast<uint32>(masked & 0xFF);
			auto red = static_cast<uint32>((masked >> 16) & 0xFF);
			auto green = static_cast<uint32>((masked >> 32) & 0xFF);
			auto blue = static_cast<uint32>((masked >> 48) & 0xFF);
			*resultInts = blue | (green << 8) | (red << 16) | (alpha << 24);
			maskBytes += maskBytesPerPixel;
			resultInts += resultIntsPerPixel;
		}
		maskBytes += maskBytesAdded;
		resultInts += resultIntsAdded;
	}

	outResult->setDevicePixelRatio(src.devicePixelRatio());
}

namespace internal {

QImage createCircleMask(int size, QColor bg, QColor fg) {
	int realSize = size * cIntRetinaFactor();
#ifndef OS_MAC_OLD
	auto result = QImage(realSize, realSize, QImage::Format::Format_Grayscale8);
#else // OS_MAC_OLD
	auto result = QImage(realSize, realSize, QImage::Format::Format_RGB32);
#endif // OS_MAC_OLD
	{
		QPainter pcircle(&result);
		pcircle.setRenderHint(QPainter::HighQualityAntialiasing, true);
		pcircle.fillRect(0, 0, realSize, realSize, bg);
		pcircle.setPen(Qt::NoPen);
		pcircle.setBrush(fg);
		pcircle.drawEllipse(0, 0, realSize, realSize);
	}
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

} // namespace internal
} // namespace style
