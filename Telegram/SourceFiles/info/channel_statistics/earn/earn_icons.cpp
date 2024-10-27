/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/earn/earn_icons.h"

#include "ui/rect.h"

#include <QtSvg/QSvgRenderer>

namespace Ui::Earn {
namespace {

[[nodiscard]] QByteArray CurrencySvg(const QColor &c) {
	const auto color = u"rgb(%1,%2,%3)"_q
		.arg(c.red())
		.arg(c.green())
		.arg(c.blue())
		.toUtf8();
	return R"(
<svg width="72px" height="72px" viewBox="0 0 72 72">
    <g stroke="none" stroke-width="1" fill="none" fill-rule="evenodd">
        <g transform="translate(9.000000, 14.000000)
        " stroke-width="7.2" stroke=")" + color + R"(">
            <path d="M2.96014341,0 L50.9898193,0 C51.9732032,-7.06402744e-15
 52.7703933,0.797190129 52.7703933,1.78057399 C52.7703933,2.08038611
 52.6946886,2.3753442 52.5502994,2.63809702 L29.699977,44.2200383
 C28.7527832,45.9436969 26.5876295,46.5731461 24.8639708,45.6259523
 C24.2556953,45.2916896 23.7583564,44.7869606 23.4331014,44.1738213
 L1.38718565,2.61498853 C0.926351231,1.74626794 1.25700829,0.668450654
 2.12572888,0.20761623 C2.38272962,0.0712838007 2.6692209,4.97530809e-16
 2.96014341,0 Z"></path>
            <line x1="27" y1="44.4532875" x2="27" y2="0"></line>
        </g>
    </g>
</svg>)";
}

} // namespace

QImage IconCurrencyColored(
		const style::font &font,
		const QColor &c) {
	const auto s = Size(font->ascent);
	auto svg = QSvgRenderer(CurrencySvg(c));
	auto image = QImage(
		s * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		svg.render(&p, Rect(s));
	}
	return image;
}

} // namespace Ui::Earn
