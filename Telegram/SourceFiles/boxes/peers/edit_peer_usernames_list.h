/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

class PeerData;

namespace Ui {
class VerticalLayout;
class VerticalLayoutReorder;
class Show;
} // namespace Ui

namespace Data {
struct Username;
} // namespace Data

class UsernamesList final : public Ui::RpWidget {
public:
	UsernamesList(
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::Show> show);

	[[nodiscard]] rpl::producer<> save();
	[[nodiscard]] std::vector<QString> order() const;

private:
	void rebuild(const std::vector<Data::Username> &usernames);
	void load();

	class Row;

	base::unique_qptr<Ui::VerticalLayout> _container;
	std::unique_ptr<Ui::VerticalLayoutReorder> _reorder;
	std::shared_ptr<Ui::Show> _show;
	const not_null<PeerData*> _peer;
	std::vector<Row*> _rows;

	int _reordering = 0;

	rpl::lifetime _loadLifetime;
	rpl::lifetime _toggleLifetime;

};
