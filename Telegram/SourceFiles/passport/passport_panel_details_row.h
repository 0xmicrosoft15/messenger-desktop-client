/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "boxes/abstract_box.h"

namespace Ui {
class InputField;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Passport {

class PanelController;

enum class PanelDetailsType {
	Text,
	Country,
	Date,
	Gender,
};

class PanelLabel : public Ui::PaddingWrap<Ui::FlatLabel> {
public:
	using PaddingWrap::PaddingWrap;

	int naturalWidth() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<BoxContentDivider> _background = object_ptr<BoxContentDivider>(this);

};

class PanelDetailsRow : public Ui::RpWidget {
public:
	using Type = PanelDetailsType;

	PanelDetailsRow(
		QWidget *parent,
		const QString &label);

	static object_ptr<PanelDetailsRow> Create(
		QWidget *parent,
		Type type,
		not_null<PanelController*> controller,
		const QString &label,
		const QString &value,
		const QString &error);

	virtual bool setFocusFast();
	virtual rpl::producer<QString> value() const = 0;
	virtual QString valueCurrent() const = 0;
	void showError(const QString &error);
	void hideError();
	void finishAnimating();

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	virtual int resizeInner(int left, int top, int width) = 0;
	virtual void showInnerError() = 0;
	virtual void finishInnerAnimating() = 0;

	void startErrorAnimation(bool shown);

	QString _label;
	object_ptr<Ui::SlideWrap<Ui::FlatLabel>> _error = { nullptr };
	bool _errorShown = false;
	Animation _errorAnimation;

};

} // namespace Passport
