/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "twidget.h"

#include "mainwindow.h"
#include "core/application.h"
#include "platform/platform_info.h"

#include <QtGui/QWindow>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

class WidgetCreator : public QWidget {
public:
	static void Create(not_null<QWidget*> widget) {
		volatile auto unknown = widget.get();
		static_cast<WidgetCreator*>(unknown)->create();
	}

};

void CreateWidgetStateRecursive(not_null<QWidget*> target) {
	if (!target->testAttribute(Qt::WA_WState_Created)) {
		if (!target->isWindow()) {
			CreateWidgetStateRecursive(target->parentWidget());
			WidgetCreator::Create(target);
		} else if (!Platform::IsMac() || Platform::IsMac10_7OrGreater()) {
			WidgetCreator::Create(target);
		}
	}
}

void SendPendingEventsRecursive(QWidget *target, bool parentHiddenFlag) {
	auto wasVisible = target->isVisible();
	if (!wasVisible) {
		target->setAttribute(Qt::WA_WState_Visible, true);
	}
	if (target->testAttribute(Qt::WA_PendingMoveEvent)) {
		target->setAttribute(Qt::WA_PendingMoveEvent, false);
		QMoveEvent e(target->pos(), QPoint());
		QCoreApplication::sendEvent(target, &e);
	}
	if (target->testAttribute(Qt::WA_PendingResizeEvent)) {
		target->setAttribute(Qt::WA_PendingResizeEvent, false);
		QResizeEvent e(target->size(), QSize());
		QCoreApplication::sendEvent(target, &e);
	}

	auto removeVisibleFlag = [&] {
		return parentHiddenFlag
			|| target->testAttribute(Qt::WA_WState_Hidden);
	};

	auto &children = target->children();
	for (auto i = 0; i < children.size(); ++i) {
		auto child = children[i];
		if (child->isWidgetType()) {
			auto widget = static_cast<QWidget*>(child);
			if (!widget->isWindow()) {
				if (!widget->testAttribute(Qt::WA_WState_Created)) {
					WidgetCreator::Create(widget);
				}
				SendPendingEventsRecursive(widget, removeVisibleFlag());
			}
		}
	}

	if (removeVisibleFlag()) {
		target->setAttribute(Qt::WA_WState_Visible, false);
	}
}

} // namespace

void SendPendingMoveResizeEvents(not_null<QWidget*> target) {
	CreateWidgetStateRecursive(target);
	SendPendingEventsRecursive(target, !target->isVisible());
}

void MarkDirtyOpaqueChildrenRecursive(not_null<QWidget*> target) {
	target->resize(target->size()); // Calls setDirtyOpaqueRegion().
	for (const auto child : target->children()) {
		if (const auto widget = qobject_cast<QWidget*>(child)) {
			MarkDirtyOpaqueChildrenRecursive(widget);
		}
	}
}

QPixmap GrabWidget(not_null<QWidget*> target, QRect rect, QColor bg) {
	SendPendingMoveResizeEvents(target);
	if (rect.isNull()) {
		rect = target->rect();
	}

	auto result = QPixmap(rect.size() * cIntRetinaFactor());
	result.setDevicePixelRatio(cRetinaFactor());
	if (!target->testAttribute(Qt::WA_OpaquePaintEvent)) {
		result.fill(bg);
	}
	{
		QPainter p(&result);
		RenderWidget(p, target, QPoint(), rect);
	}
	return result;
}

QImage GrabWidgetToImage(not_null<QWidget*> target, QRect rect, QColor bg) {
	SendPendingMoveResizeEvents(target);
	if (rect.isNull()) {
		rect = target->rect();
	}

	auto result = QImage(
		rect.size() * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	if (!target->testAttribute(Qt::WA_OpaquePaintEvent)) {
		result.fill(bg);
	}
	{
		QPainter p(&result);
		RenderWidget(p, target, QPoint(), rect);
	}
	return result;
}

void RenderWidget(
		QPainter &painter,
		not_null<QWidget*> source,
		const QPoint &targetOffset,
		const QRegion &sourceRegion,
		QWidget::RenderFlags renderFlags) {
	const auto visible = source->isVisible();
	source->render(&painter, targetOffset, sourceRegion, renderFlags);
	if (!visible) {
		MarkDirtyOpaqueChildrenRecursive(source);
	}
}

void ForceFullRepaint(not_null<QWidget*> widget) {
	const auto refresher = std::make_unique<QWidget>(widget);
	refresher->setGeometry(widget->rect());
	refresher->show();
}

void PostponeCall(FnMut<void()> &&callable) {
	Core::App().postponeCall(std::move(callable));
}

} // namespace Ui

void sendSynteticMouseEvent(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPoint &globalPoint) {
	if (auto windowHandle = widget->window()->windowHandle()) {
		auto localPoint = windowHandle->mapFromGlobal(globalPoint);
		QMouseEvent ev(type
			, localPoint
			, localPoint
			, globalPoint
			, button
			, QGuiApplication::mouseButtons() | button
			, QGuiApplication::keyboardModifiers()
#ifndef OS_MAC_OLD
			, Qt::MouseEventSynthesizedByApplication
#endif // OS_MAC_OLD
		);
		ev.setTimestamp(crl::now());
		QGuiApplication::sendEvent(windowHandle, &ev);
	}
}
