/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Storage {
class Accounts;
enum class StartResult : uchar;
} // namespace Storage

namespace Main {

class Account;
class Session;

class Accounts final {
public:
	explicit Accounts(const QString &dataName);
	~Accounts();

	[[nodiscard]] bool started() const;
	Storage::StartResult start(const QByteArray &passcode);

	[[nodiscard]] Storage::Accounts &local() const {
		return *_local;
	}

	[[nodiscard]] auto list() const
		-> const base::flat_map<int, std::unique_ptr<Account>> &;
	[[nodiscard]] rpl::producer<Account*> activeValue() const;

	// Expects(started());
	[[nodiscard]] Account &active() const;
	[[nodiscard]] rpl::producer<not_null<Account*>> activeChanges() const;

	[[nodiscard]] rpl::producer<Session*> activeSessionValue() const;
	[[nodiscard]] rpl::producer<Session*> activeSessionChanges() const;

	[[nodiscard]] int add();
	void activate(int index);

private:
	const QString _dataName;
	const std::unique_ptr<Storage::Accounts> _local;

	base::flat_map<int, std::unique_ptr<Account>> _accounts;
	rpl::variable<Account*> _active = nullptr;
	int _activeIndex = 0;

	rpl::event_stream<Session*> _activeSessions;

	rpl::lifetime _activeLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Main
