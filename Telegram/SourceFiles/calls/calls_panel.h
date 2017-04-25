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
#pragma once

#include "base/weak_unique_ptr.h"
#include "calls/calls_call.h"
#include "base/timer.h"

namespace Ui {
class IconButton;
class FlatLabel;
} // namespace Ui

namespace Calls {

class Panel : public TWidget, private base::Subscriber {
public:
	Panel(gsl::not_null<Call*> call);

	void showAndActivate();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	bool event(QEvent *e) override;

private:
	using State = Call::State;
	using Type = Call::Type;

	void initControls();
	void initLayout();
	void initGeometry();
	void hideDeactivated();
	void createBottomImage();
	void createDefaultCacheImage();
	void refreshCacheImageUserPhoto();

	void processUserPhoto();
	void refreshUserPhoto();
	bool isGoodUserPhoto(PhotoData *photo);
	void createUserpicCache(ImagePtr image);

	void updateControlsGeometry();
	void updateStatusGeometry();
	void stateChanged(State state);
	void updateStatusText(State state);
	void startDurationUpdateTimer(TimeMs currentDuration);

	base::weak_unique_ptr<Call> _call;
	gsl::not_null<UserData*> _user;

	bool _useTransparency = true;
	style::margins _padding;
	int _contentTop = 0;

	bool _dragging = false;
	QPoint _dragStartMousePosition;
	QPoint _dragStartMyPosition;

	class Button;
	object_ptr<Ui::IconButton> _close = { nullptr };
	object_ptr<Button> _answer = { nullptr };
	object_ptr<Button> _hangup;
	object_ptr<Ui::IconButton> _mute;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _status;
	std::vector<EmojiPtr> _fingerprint;

	base::Timer _updateDurationTimer;

	QPixmap _userPhoto;
	PhotoId _userPhotoId = 0;
	bool _userPhotoFull = false;

	QPixmap _bottomCache;

	QPixmap _cache;

};

} // namespace Calls
