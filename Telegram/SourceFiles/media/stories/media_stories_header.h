/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/userpic_view.h"

namespace Data {
enum class StoryPrivacy : uchar;
} // namespace Data

namespace Ui {
class RpWidget;
class FlatLabel;
} // namespace Ui

namespace Media::Stories {

class Controller;

struct HeaderData {
	not_null<UserData*> user;
	TimeId date = 0;
	int fullIndex = 0;
	int fullCount = 0;
	Data::StoryPrivacy privacy = {};
	bool edited = false;
	bool video = false;
	bool silent = false;

	friend inline auto operator<=>(HeaderData, HeaderData) = default;
	friend inline bool operator==(HeaderData, HeaderData) = default;
};

class Header final {
public:
	explicit Header(not_null<Controller*> controller);
	~Header();

	void show(HeaderData data);
	void raise();

private:
	void updateDateText();

	const not_null<Controller*> _controller;

	std::unique_ptr<Ui::RpWidget> _widget;
	std::unique_ptr<Ui::FlatLabel> _date;
	std::optional<HeaderData> _data;
	base::Timer _dateUpdateTimer;

};

} // namespace Media::Stories
