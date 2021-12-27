/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/scroll_area.h"

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace Data {
struct Reaction;
class DocumentMedia;
} // namespace Data

namespace HistoryView {
using PaintContext = Ui::ChatPaintContext;
struct TextState;
} // namespace HistoryView

namespace HistoryView::Reactions {

enum class ButtonStyle {
	Bubble,
};

enum class ExpandDirection {
	Up,
	Down,
};

struct ButtonParameters {
	[[nodiscard]] ButtonParameters translated(QPoint delta) const {
		auto result = *this;
		result.center += delta;
		result.pointer += delta;
		return result;
	}

	FullMsgId context;
	QPoint center;
	QPoint pointer;
	ButtonStyle style = ButtonStyle::Bubble;
	int reactionsCount = 1;
	int visibleTop = 0;
	int visibleBottom = 0;
	bool outbg = false;
};

enum class ButtonState {
	Hidden,
	Shown,
	Active,
	Inside,
};

class Button final {
public:
	Button(Fn<void(QRect)> update, ButtonParameters parameters);
	~Button();

	void applyParameters(ButtonParameters parameters);

	using State = ButtonState;
	void applyState(State state);

	[[nodiscard]] bool outbg() const;
	[[nodiscard]] bool expandUp() const;
	[[nodiscard]] bool isHidden() const;
	[[nodiscard]] QRect geometry() const;
	[[nodiscard]] int scroll() const;
	[[nodiscard]] float64 currentScale() const;
	[[nodiscard]] bool consumeWheelEvent(not_null<QWheelEvent*> e);

	[[nodiscard]] static float64 ScaleForState(State state);
	[[nodiscard]] static float64 OpacityForScale(float64 scale);

private:
	void updateGeometry(Fn<void(QRect)> update);
	void applyState(State satte, Fn<void(QRect)> update);
	void applyParameters(
		ButtonParameters parameters,
		Fn<void(QRect)> update);
	void updateExpandDirection(const ButtonParameters &parameters);

	const Fn<void(QRect)> _update;
	State _state = State::Hidden;
	Ui::Animations::Simple _scaleAnimation;
	Ui::Animations::Simple _heightAnimation;

	QRect _collapsed;
	QRect _geometry;
	int _expandedInnerHeight = 0;
	int _expandedHeight = 0;
	int _finalHeight = 0;
	int _scroll = 0;
	ExpandDirection _expandDirection = ExpandDirection::Up;
	bool _outbg = false;

};

class Manager final : public base::has_weak_ptr {
public:
	Manager(
		QWidget *wheelEventsTarget,
		Fn<void(QRect)> buttonUpdate);
	~Manager();

	void applyList(std::vector<Data::Reaction> list);

	void updateButton(ButtonParameters parameters);
	void paintButtons(Painter &p, const PaintContext &context);
	[[nodiscard]] TextState buttonTextState(QPoint position) const;
	void remove(FullMsgId context);

	[[nodiscard]] bool consumeWheelEvent(not_null<QWheelEvent*> e);

	struct Chosen {
		FullMsgId context;
		QString emoji;
	};
	[[nodiscard]] rpl::producer<Chosen> chosen() const {
		return _chosen.events();
	}

private:
	struct OtherReactionImage {
		QImage image;
		std::shared_ptr<Data::DocumentMedia> media;
	};
	static constexpr auto kFramesCount = 30;

	void stealWheelEvents(not_null<QWidget*> target);

	[[nodiscard]] bool overCurrentButton(QPoint position) const;

	void removeStaleButtons();
	void paintButton(
		Painter &p,
		const PaintContext &context,
		not_null<Button*> button);
	void paintButton(
		Painter &p,
		const PaintContext &context,
		not_null<Button*> button,
		int frame,
		float64 scale);
	void paintAllEmoji(
		Painter &p,
		not_null<Button*> button,
		float64 scale,
		QPoint mainEmojiPosition);
	void paintLongImage(
		Painter &p,
		QRect geometry,
		const QImage &image,
		QRect source);

	void setMainReactionImage(QImage image);
	void applyPatternedShadow(const QColor &shadow);
	[[nodiscard]] QRect cacheRect(int frameIndex, int columnIndex) const;
	QRect validateShadow(
		int frameIndex,
		float64 scale,
		const QColor &shadow);
	QRect validateEmoji(int frameIndex, float64 scale);
	QRect validateFrame(
		bool outbg,
		int frameIndex,
		float64 scale,
		const QColor &background,
		const QColor &shadow);
	QRect validateMask(int frameIndex, float64 scale);
	void validateCacheForPattern(
		int frameIndex,
		float64 scale,
		const QRect &geometry,
		const PaintContext &context);

	[[nodiscard]] QMarginsF innerMargins() const;
	[[nodiscard]] QRectF buttonInner() const;
	[[nodiscard]] QRectF buttonInner(not_null<Button*> button) const;
	void loadOtherReactions();
	void checkOtherReactions();
	[[nodiscard]] ClickHandlerPtr computeButtonLink(QPoint position) const;
	[[nodiscard]] ClickHandlerPtr resolveButtonLink(
		const Data::Reaction &reaction) const;

	rpl::event_stream<Chosen> _chosen;
	std::vector<Data::Reaction> _list;
	mutable std::vector<ClickHandlerPtr> _links;
	QSize _outer;
	QRectF _inner;
	QRect _innerActive;
	QImage _cacheInOut;
	QImage _cacheParts;
	QImage _cacheForPattern;
	QImage _shadowBuffer;
	std::array<bool, kFramesCount> _validIn = { { false } };
	std::array<bool, kFramesCount> _validOut = { { false } };
	std::array<bool, kFramesCount> _validShadow = { { false } };
	std::array<bool, kFramesCount> _validEmoji = { { false } };
	std::array<bool, kFramesCount> _validMask = { { false } };
	QColor _backgroundIn;
	QColor _backgroundOut;
	QColor _shadow;

	std::shared_ptr<Data::DocumentMedia> _mainReactionMedia;
	QImage _mainReactionImage;
	rpl::lifetime _mainReactionLifetime;

	base::flat_map<
		not_null<DocumentData*>,
		OtherReactionImage> _otherReactions;
	rpl::lifetime _otherReactionsLifetime;

	const Fn<void(QRect)> _buttonUpdate;
	std::unique_ptr<Button> _button;
	std::vector<std::unique_ptr<Button>> _buttonHiding;
	FullMsgId _buttonContext;
	mutable base::flat_map<QString, ClickHandlerPtr> _reactionsLinks;

};

} // namespace HistoryView
