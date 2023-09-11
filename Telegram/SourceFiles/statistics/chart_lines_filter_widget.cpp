/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_lines_filter_widget.h"

#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_basic.h"
#include "styles/style_statistics.h"
#include "styles/style_widgets.h"

namespace Statistic {
namespace {
constexpr auto kShiftDuration = crl::time(300);
} // namespace

class ChartLinesFilterWidget::FlatCheckbox final : public Ui::AbstractButton {
public:
	FlatCheckbox(
		not_null<Ui::RpWidget*> parent,
		const QString &text,
		QColor activeColor);

	void shake();
	void setChecked(bool value, bool animated);
	[[nodiscard]] bool checked() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const QColor _activeColor;
	const QColor _inactiveColor;
	Ui::Text::String _text;

	Ui::Animations::Simple _animation;

	struct {
		Ui::Animations::Simple animation;
		int shift = 0;
	} _shake;

	bool _checked = true;

};

ChartLinesFilterWidget::FlatCheckbox::FlatCheckbox(
	not_null<Ui::RpWidget*> parent,
	const QString &text,
	QColor activeColor)
: Ui::AbstractButton(parent)
, _activeColor(activeColor)
, _inactiveColor(st::boxBg->c)
, _text(st::statisticsDetailsBottomCaptionStyle, text) {
	const auto &margins = st::statisticsChartFlatCheckboxMargins;
	const auto h = _text.minHeight() + rect::m::sum::v(margins) * 2;
	resize(
		_text.maxWidth()
			+ rect::m::sum::h(margins)
			+ h
			+ st::statisticsChartFlatCheckboxCheckWidth * 3,
		h);
}

void ChartLinesFilterWidget::FlatCheckbox::setChecked(
		bool value,
		bool animated) {
	if (_checked == value) {
		return;
	}
	_checked = value;
	if (!animated) {
		_animation.stop();
	} else {
		const auto from = value ? 0. : 1.;
		const auto to = value ? 1. : 0.;
		_animation.start([=] { update(); }, from, to, kShiftDuration);
	}
}

bool ChartLinesFilterWidget::FlatCheckbox::checked() const {
	return _checked;
}

void ChartLinesFilterWidget::FlatCheckbox::shake() {
	if (_shake.animation.animating()) {
		return;
	}
	constexpr auto kShiftProgress = 6;
	constexpr auto kSegmentsCount = 5;
	const auto refresh = [=] {
		const auto fullProgress = _shake.animation.value(1.) * kShiftProgress;
		const auto segment = std::clamp(
			int(std::floor(fullProgress)),
			0,
			kSegmentsCount);
		const auto part = fullProgress - segment;
		const auto from = (segment == 0)
			? 0.
			: (segment == 1 || segment == 3 || segment == 5)
			? 1.
			: -1.;
		const auto to = (segment == 0 || segment == 2 || segment == 4)
			? 1.
			: (segment == 1 || segment == 3)
			? -1.
			: 0.;
		const auto shift = from * (1. - part) + to * part;
		_shake.shift = int(base::SafeRound(shift * st::shakeShift));
		update();
	};
	_shake.animation.start(refresh, 0., 1., kShiftDuration);
}

void ChartLinesFilterWidget::FlatCheckbox::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto progress = _animation.value(_checked ? 1. : 0.);

	p.translate(_shake.shift, 0);

	const auto checkWidth = st::statisticsChartFlatCheckboxCheckWidth;
	const auto r = rect() - st::statisticsChartFlatCheckboxMargins;
	const auto heightHalf = r.height() / 2.;
	const auto textX = anim::interpolate(
		r.center().x() - _text.maxWidth() / 2.,
		r.x() + heightHalf + checkWidth * 5,
		progress);
	const auto textY = (r - st::statisticsChartFlatCheckboxMargins).y();
	p.fillRect(r, Qt::transparent);

	constexpr auto kCheckPartProgress = 0.5;
	const auto checkProgress = progress / kCheckPartProgress;
	const auto textColor = (progress <= kCheckPartProgress)
		? anim::color(_activeColor, _inactiveColor, checkProgress)
		: _inactiveColor;
	const auto fillColor = (progress <= kCheckPartProgress)
		? anim::color(_inactiveColor, _activeColor, checkProgress)
		: _activeColor;

	p.setPen(QPen(_activeColor, st::statisticsChartLineWidth));
	p.setBrush(fillColor);
	const auto radius = r.height() / 2.;
	{
		auto hq = PainterHighQualityEnabler(p);
		p.drawRoundedRect(r, radius, radius);
	}

	p.setPen(textColor);
	const auto textContext = Ui::Text::PaintContext{
		.position = QPoint(textX, textY),
		.availableWidth = width(),
	};
	_text.draw(p, textContext);

	if (progress > kCheckPartProgress) {
		p.setPen(QPen(textColor, st::statisticsChartLineWidth));
		const auto bounceProgress = checkProgress - 1.;
		const auto start = QPoint(
			r.x() + heightHalf + checkWidth,
			textY + _text.style()->font->ascent);
		p.translate(start);
		p.drawLine({}, -QPoint(checkWidth, checkWidth) * bounceProgress);
		p.drawLine({}, QPoint(checkWidth, -checkWidth) * bounceProgress * 2);
	}
}

ChartLinesFilterWidget::ChartLinesFilterWidget(
	not_null<Ui::RpWidget*> parent)
: Ui::RpWidget(parent) {
}

void ChartLinesFilterWidget::fillButtons(
		const std::vector<QString> &texts,
		const std::vector<QColor> &colors,
		const std::vector<int> &ids,
		int outerWidth) {
	Expects(texts.size() == colors.size());
	_buttons.clear();

	_buttons.reserve(texts.size());
	auto maxRight = 0;
	for (auto i = 0; i < texts.size(); i++) {
		auto button = base::make_unique_q<FlatCheckbox>(
			this,
			texts[i],
			colors[i]);
		button->show();
		if (!i) {
			button->move(0, 0);
		} else {
			const auto lastRaw = _buttons.back().get();
			const auto lastLeft = rect::right(lastRaw);
			const auto isOut = (lastLeft + button->width() > outerWidth);
			const auto left = isOut ? 0 : lastLeft;
			const auto top = isOut ? rect::bottom(lastRaw) : lastRaw->y();
			button->move(left, top);
		}
		const auto id = ids[i];
		button->setClickedCallback([=, raw = button.get()] {
			const auto checked = !raw->checked();
			if (!checked) {
				const auto cancel = [&] {
					for (const auto &b : _buttons) {
						if (b.get() == raw) {
							continue;
						}
						if (b->checked()) {
							return false;
						}
					}
					return true;
				}();
				if (cancel) {
					raw->shake();
					return;
				}
			}
			raw->setChecked(checked, true);
			_buttonEnabledChanges.fire({ .id = id, .enabled = checked });
		});
		maxRight = std::max(maxRight, rect::right(button.get()));

		_buttons.push_back(std::move(button));
	}

	resize(maxRight, rect::bottom(_buttons.back().get()));
}

auto ChartLinesFilterWidget::buttonEnabledChanges() const
-> rpl::producer<ChartLinesFilterWidget::Entry> {
	return _buttonEnabledChanges.events();
}

} // namespace Statistic
