/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

class QJsonObject;

namespace Stripe {

enum class CardBrand {
	Visa,
	Amex,
	MasterCard,
	Discover,
	JCB,
	DinersClub,
	Unknown,
};

enum class CardFundingType {
	Debit,
	Credit,
	Prepaid,
	Other,
};

class Card final {
public:
	Card(const Card &other) = default;
	Card &operator=(const Card &other) = default;
	Card(Card &&other) = default;
	Card &operator=(Card &&other) = default;
	~Card() = default;

	[[nodiscard]] static Card Empty();
	[[nodiscard]] static Card DecodedObjectFromAPIResponse(
		QJsonObject object);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

private:
	Card(
		QString id,
		QString last4,
		CardBrand brand,
		quint32 expMonth,
		quint32 expYear);

	QString _cardId;
	QString _name;
	QString _last4;
	QString _dynamicLast4;
	CardBrand _brand = CardBrand::Unknown;
	CardFundingType _funding = CardFundingType::Other;
	QString _fingerprint;
	QString _country;
	QString _currency;
	quint32 _expMonth = 0;
	quint32 _expYear = 0;
	QString _addressLine1;
	QString _addressLine2;
	QString _addressCity;
	QString _addressState;
	QString _addressZip;
	QString _addressCountry;

};

[[nodiscard]] QString CardBrandToString(CardBrand brand);

} // namespace Stripe
