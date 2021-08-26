/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

namespace Countries {

struct Info {
	QString name;
	QString iso2;
	QString code;
	QString alternativeName;
};

class CountriesInstance final {
public:
	using Map = QHash<QString, const Info *>;

	CountriesInstance();
	[[nodiscard]] const std::vector<Info> &list();
	void setList(std::vector<Info> &&infos);

	[[nodiscard]] const Map &byCode();
	[[nodiscard]] const Map &byISO2();

	[[nodiscard]] QString validPhoneCode(QString fullCode);
	[[nodiscard]] QString countryNameByISO2(const QString &iso);
	[[nodiscard]] QString countryISO2ByPhone(const QString &phone);

private:
	std::vector<Info> _list;

	Map _byCode;
	Map _byISO2;

};

CountriesInstance &Instance();

} // namespace Countries
