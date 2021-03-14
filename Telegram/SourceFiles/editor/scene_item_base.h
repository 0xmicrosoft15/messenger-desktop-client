/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QGraphicsItem>

class QGraphicsSceneHoverEvent;
class QGraphicsSceneMouseEvent;
class QStyleOptionGraphicsItem;

namespace Editor {

class NumberedItem : public QGraphicsItem {
public:
	using QGraphicsItem::QGraphicsItem;

	void setNumber(int number);
	[[nodiscard]] int number() const;
private:
	int _number = 0;
};

class ItemBase : public NumberedItem {
public:
	enum { Type = UserType + 1 };

	ItemBase(std::shared_ptr<float64> zPtr, int size, int x, int y);
	QRectF boundingRect() const override;
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;
protected:
	enum HandleType {
		None,
		Left,
		Right,
	};
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

	QRectF innerRect() const;
	int size() const;

private:
	HandleType handleType(const QPointF &pos) const;
	QRectF rightHandleRect() const;
	QRectF leftHandleRect() const;
	bool isHandling() const;

	const std::shared_ptr<float64> _lastZ;
	const int _handleSize;
	const QMargins _innerMargins;
	const QPen _selectPen;
	const QPen _selectPenInactive;
	const QPen _handlePen;

	int _size;
	HandleType _handle = HandleType::None;

};

} // namespace Editor
