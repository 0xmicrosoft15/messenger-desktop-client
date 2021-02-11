/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace HistoryView::Controls {

class TTLButton final {
public:
	TTLButton(not_null<QWidget*> parent, not_null<PeerData*> peer);

	void show();
	void hide();
	void move(int x, int y);

	[[nodiscard]] int width() const;

private:
	Ui::IconButton _button;

};

} // namespace HistoryView::Controls
