/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Media::Streaming {
class Document;
class Instance;
} // namespace Media::Streaming

enum class PeerShortInfoType {
	User,
	Group,
	Channel,
};

struct PeerShortInfoFields {
	QString name;
	QString phone;
	QString link;
	TextWithEntities about;
	QString username;
};

struct PeerShortInfoUserpic {
	int index = 0;
	int count = 0;

	QImage photo;
	float64 photoLoadingProgress = 0.;
	std::shared_ptr<Media::Streaming::Document> videoDocument;
};

class PeerShortInfoBox final : public Ui::BoxContent {
public:
	PeerShortInfoBox(
		QWidget*,
		PeerShortInfoType type,
		rpl::producer<PeerShortInfoFields> fields,
		rpl::producer<QString> status,
		rpl::producer<PeerShortInfoUserpic> userpic);
	~PeerShortInfoBox();

	[[nodiscard]] rpl::producer<> openRequests() const;

private:
	void prepare() override;
	RectParts customCornersFilling() override;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	[[nodiscard]] rpl::producer<QString> nameValue() const;
	void applyUserpic(PeerShortInfoUserpic &&value);

	const PeerShortInfoType _type = PeerShortInfoType::User;

	rpl::variable<PeerShortInfoFields> _fields;

	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _status;

	QImage _userpicImage;
	std::unique_ptr<Media::Streaming::Instance> _videoInstance;

	rpl::event_stream<> _openRequests;

};
