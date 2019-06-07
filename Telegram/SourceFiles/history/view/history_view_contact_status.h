/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/shadow.h"

namespace Ui {
class FlatButton;
class IconButton;
} // namespace Ui

namespace HistoryView {

class ContactStatus final {
public:
	ContactStatus(not_null<Ui::RpWidget*> parent, not_null<PeerData*> peer);

	void show();
	void raise();

	void move(int x, int y);
	int height() const;
	rpl::producer<int> heightValue() const;

private:
	enum class State {
		None,
		ReportSpam,
		BlockOrAdd,
		SharePhoneNumber,
	};

	class Bar : public Ui::RpWidget {
	public:
		explicit Bar(QWidget *parent);

		void showState(State state);

	protected:
		void resizeEvent(QResizeEvent *e) override;

	private:
		object_ptr<Ui::FlatButton> _block;
		object_ptr<Ui::FlatButton> _add;
		object_ptr<Ui::FlatButton> _share;
		object_ptr<Ui::FlatButton> _report;
		object_ptr<Ui::IconButton> _close;

	};

	void setupWidgets(not_null<Ui::RpWidget*> parent);
	void setupState(not_null<PeerData*> peer);

	static rpl::producer<State> PeerState(not_null<PeerData*> peer);

	State _state = State::None;
	Ui::SlideWrap<Bar> _bar;
	Ui::PlainShadow _shadow;
	bool _shown = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
