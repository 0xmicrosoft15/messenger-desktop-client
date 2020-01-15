/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"
#include "info/info_controller.h"

namespace Info {
namespace Polls {

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(not_null<PollData*> poll, FullMsgId contextId);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	~Memento();

};

class Widget final : public ContentWidget {
public:
	Widget(QWidget *parent, not_null<Controller*> controller);

	[[nodiscard]] not_null<PollData*> poll() const;
	[[nodiscard]] FullMsgId contextId() const;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::unique_ptr<ContentMemento> doCreateMemento() override;

	not_null<InnerWidget*> _inner;

};

} // namespace Settings
} // namespace Info
