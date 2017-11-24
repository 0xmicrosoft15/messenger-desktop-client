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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/effects/numbers_animation.h"

#include "lang/lang_tag.h"
#include "styles/style_widgets.h"

namespace Ui {

NumbersAnimation::NumbersAnimation(
	const style::font &font,
	base::lambda<void()> animationCallback)
: _font(font)
, _animationCallback(std::move(animationCallback)) {
	for (auto ch = '0'; ch != '9'; ++ch) {
		accumulate_max(_digitWidth, _font->m.width(ch));
	}
}

void NumbersAnimation::setText(const QString &text, int value) {
	if (_a_ready.animating(getms())) {
		_delayedText = text;
		_delayedValue = value;
	} else {
		realSetText(text, value);
	}
}

void NumbersAnimation::animationCallback() {
	if (_animationCallback) {
		_animationCallback();
	}
	if (_widthChangedCallback) {
		_widthChangedCallback();
	}
	if (!_a_ready.animating()) {
		if (!_delayedText.isEmpty()) {
			setText(_delayedText, _delayedValue);
		}
	}
}

void NumbersAnimation::realSetText(QString text, int value) {
	_delayedText = QString();
	_delayedValue = 0;

	_growing = (value > _value);
	_value = value;

	auto newSize = text.size();
	while (_digits.size() < newSize) {
		_digits.push_front(Digit());
	}
	while (_digits.size() > newSize && !_digits.front().to.unicode()) {
		_digits.pop_front();
	}
	auto oldSize = _digits.size();
	auto animating = false;
	for (auto i = 0, size = _digits.size(); i != size; ++i) {
		auto &digit = _digits[i];
		digit.from = digit.to;
		digit.fromWidth = digit.toWidth;
		digit.to = (newSize + i < size) ? QChar(0) : text[newSize + i - size];
		digit.toWidth = digit.to.unicode() ? _font->m.width(digit.to) : 0;
		if (digit.from != digit.to) {
			animating = true;
		}
		if (!digit.from.unicode()) {
			--oldSize;
		}
	}
	_fromWidth = oldSize * _digitWidth;
	_toWidth = newSize * _digitWidth;
	if (animating) {
		_a_ready.start(
			[this] { animationCallback(); },
			0.,
			1.,
			st::slideWrapDuration);
	}
}

int NumbersAnimation::countWidth() const {
	return anim::interpolate(
		_fromWidth,
		_toWidth,
		anim::easeOutCirc(1., _a_ready.current(1.)));
}

void NumbersAnimation::stepAnimation(TimeMs ms) {
	_a_ready.step(ms);
}

void NumbersAnimation::finishAnimating() {
	auto width = countWidth();
	_a_ready.finish();
	if (_widthChangedCallback && countWidth() != width) {
		_widthChangedCallback();
	}
	if (!_delayedText.isEmpty()) {
		setText(_delayedText, _delayedValue);
	}
}

void NumbersAnimation::paint(Painter &p, int x, int y, int outerWidth) {
	auto digitsCount = _digits.size();
	if (!digitsCount) return;

	auto progress = anim::easeOutCirc(1., _a_ready.current(1.));
	auto width = anim::interpolate(_fromWidth, _toWidth, progress);

	QString singleChar('0');
	if (rtl()) x = outerWidth - x - width;
	x += width - _digits.size() * _digitWidth;
	auto fromTop = anim::interpolate(0, _font->height, progress) * (_growing ? 1 : -1);
	auto toTop = anim::interpolate(_font->height, 0, progress) * (_growing ? -1 : 1);
	for (auto i = 0; i != digitsCount; ++i) {
		auto &digit = _digits[i];
		auto from = digit.from;
		auto to = digit.to;
		if (from == to) {
			p.setOpacity(1.);
			singleChar[0] = from;
			p.drawText(x + (_digitWidth - digit.fromWidth) / 2, y + _font->ascent, singleChar);
		} else {
			if (from.unicode()) {
				p.setOpacity(1. - progress);
				singleChar[0] = from;
				p.drawText(x + (_digitWidth - digit.fromWidth) / 2, y + fromTop + _font->ascent, singleChar);
			}
			if (to.unicode()) {
				p.setOpacity(progress);
				singleChar[0] = to;
				p.drawText(x + (_digitWidth - digit.toWidth) / 2, y + toTop + _font->ascent, singleChar);
			}
		}
		x += _digitWidth;
	}
	p.setOpacity(1.);
}

LabelWithNumbers::LabelWithNumbers(
	QWidget *parent,
	const style::FlatLabel &st,
	int textTop,
	const StringWithNumbers &value)
: RpWidget(parent)
, _st(st)
, _textTop(textTop)
, _before(GetBefore(value))
, _after(GetAfter(value))
, _numbers(_st.style.font, [this] { update(); })
, _beforeWidth(_st.style.font->width(_before))
, _afterWidth(st.style.font->width(_after)) {
	Expects((value.offset < 0) == (value.length == 0));

	const auto numbers = GetNumbers(value);
	_numbers.setText(numbers, numbers.toInt());
	_numbers.finishAnimating();
}

QString LabelWithNumbers::GetBefore(const StringWithNumbers &value) {
	return value.text.mid(0, value.offset);
}

QString LabelWithNumbers::GetAfter(const StringWithNumbers &value) {
	return (value.offset >= 0)
		? value.text.mid(value.offset + value.length)
		: QString();
}

QString LabelWithNumbers::GetNumbers(const StringWithNumbers &value) {
	return (value.offset >= 0)
		? value.text.mid(value.offset, value.length)
		: QString();
}

void LabelWithNumbers::setValue(const StringWithNumbers &value) {
	_before = GetBefore(value);
	_after = GetAfter(value);
	const auto numbers = GetNumbers(value);
	_numbers.setText(numbers, numbers.toInt());

	const auto oldBeforeWidth = std::exchange(
		_beforeWidth,
		_st.style.font->width(_before));
	_beforeWidthAnimation.start(
		[this] { update(); },
		oldBeforeWidth,
		_beforeWidth,
		st::slideWrapDuration,
		anim::easeOutCirc);

	_afterWidth = _st.style.font->width(_after);
}

void LabelWithNumbers::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = getms();
	const auto beforeWidth = _beforeWidthAnimation.current(ms, _beforeWidth);
	_numbers.stepAnimation(ms);

	p.setFont(_st.style.font);
	p.setBrush(Qt::NoBrush);
	p.setPen(_st.textFg);
	auto left = 0;
	const auto outerWidth = width();

	p.setClipRect(0, 0, left + beforeWidth, height());
	p.drawTextLeft(left, _textTop, outerWidth, _before, _beforeWidth);
	left += beforeWidth;
	p.setClipping(false);

	_numbers.paint(p, left, _textTop, outerWidth);
	left += _numbers.countWidth();

	const auto availableWidth = outerWidth - left;
	const auto text = (availableWidth < _afterWidth)
		? _st.style.font->elided(_after, availableWidth)
		: _after;
	const auto textWidth = (availableWidth < _afterWidth) ? -1 : _afterWidth;
	p.drawTextLeft(left, _textTop, outerWidth, text, textWidth);
}

} // namespace Ui

namespace Lang {

Ui::StringWithNumbers ReplaceTag<Ui::StringWithNumbers>::Call(
		Ui::StringWithNumbers &&original,
		ushort tag,
		const Ui::StringWithNumbers &replacement) {
	original.offset = FindTagReplacementPosition(original.text, tag);
	original.text = ReplaceTag<QString>::Call(
		std::move(original.text),
		tag,
		replacement.text);
	original.length = replacement.text.size();
	return std::move(original);
}

} // namespace Lang