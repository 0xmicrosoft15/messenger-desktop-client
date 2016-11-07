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
#pragma once

#include "ui/button.h"
#include "ui/animation.h"

class FlatButton : public Button {
	Q_OBJECT

public:
	FlatButton(QWidget *parent, const QString &text, const style::flatButton &st);

	void step_appearance(float64 ms, bool timer);
	void paintEvent(QPaintEvent *e);
	void setOpacity(float64 o);
	float64 opacity() const;

	void setText(const QString &text);
	void setWidth(int32 w);

	int32 textWidth() const;

	~FlatButton() {
	}

public slots:
	void onStateChange(int oldState, ButtonStateChangeSource source);

private:
	QString _text, _textForAutoSize;
	int _width, _textWidth;

	const style::flatButton &_st;

	anim::fvalue a_over;
	Animation _a_appearance;

	float64 _opacity = 1.;

};

class LinkButton : public Button {
	Q_OBJECT

public:
	LinkButton(QWidget *parent, const QString &text, const style::linkButton &st = st::btnDefLink);

	int naturalWidth() const override;

	void setText(const QString &text);

	~LinkButton();

protected:
	void paintEvent(QPaintEvent *e) override;

public slots:
	void onStateChange(int oldState, ButtonStateChangeSource source);

private:
	QString _text;
	int _textWidth = 0;
	const style::linkButton &_st;

};

class BoxButton : public Button {
	Q_OBJECT

public:
	BoxButton(QWidget *parent, const QString &text, const style::RoundButton &st);

	void setText(const QString &text);
	void paintEvent(QPaintEvent *e) override;

	void step_over(float64 ms, bool timer);

public slots:
	void onStateChange(int oldState, ButtonStateChangeSource source);

private:
	void resizeToText();

	QString _text, _fullText;
	int32 _textWidth;

	const style::RoundButton &_st;

};
