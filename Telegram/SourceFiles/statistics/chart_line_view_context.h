/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Statistic {

class ChartLineViewContext final {
public:
	ChartLineViewContext() = default;

	void setEnabled(int id, bool enabled, crl::time now);
	[[nodiscard]] bool isEnabled(int id) const;
	[[nodiscard]] bool isFinished() const;
	[[nodiscard]] float64 alpha(int id) const;

	void tick(crl::time now);

	float64 factor = 1.;

private:
	struct Entry final {
		bool enabled = false;
		crl::time startedAt = 0;
		float64 alpha = 1.;
	};

	base::flat_map<int, Entry> _entries;
	bool _isFinished = true;

};

} // namespace Statistic
