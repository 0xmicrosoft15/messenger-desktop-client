/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_header.h"

#include "base/unixtime.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_user.h"
#include "media/stories/media_stories_controller.h"
#include "lang/lang_keys.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/box_content.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_media_view.h"

namespace Media::Stories {
namespace {

constexpr auto kNameOpacity = 1.;
constexpr auto kDateOpacity = 0.8;

struct Timestamp {
	QString text;
	TimeId changes = 0;
};

struct PrivacyBadge {
	const style::icon *icon = nullptr;
	const style::color *bg1 = nullptr;
	const style::color *bg2 = nullptr;
};

class UserpicBadge final : public Ui::RpWidget {
public:
	UserpicBadge(
		not_null<QWidget*> userpic,
		PrivacyBadge badge,
		Fn<void()> clicked);

private:
	bool eventFilter(QObject *o, QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void updateGeometry();

	const not_null<QWidget*> _userpic;
	const PrivacyBadge _badgeData;
	const std::unique_ptr<Ui::AbstractButton> _clickable;
	QRect _badge;
	QImage _layer;
	bool _grabbing = false;

};

[[nodiscard]] PrivacyBadge LookupPrivacyBadge(Data::StoryPrivacy privacy) {
	using namespace Data;
	static const auto badges = base::flat_map<StoryPrivacy, PrivacyBadge>{
		{ StoryPrivacy::CloseFriends, PrivacyBadge{
			&st::storiesBadgeCloseFriends,
			&st::historyPeer2UserpicBg,
			&st::historyPeer2UserpicBg2,
		} },
		{ StoryPrivacy::Contacts, PrivacyBadge{
			&st::storiesBadgeContacts,
			&st::historyPeer5UserpicBg,
			&st::historyPeer5UserpicBg2,
		} },
		{ StoryPrivacy::SelectedContacts, PrivacyBadge{
			&st::storiesBadgeSelectedContacts,
			&st::historyPeer8UserpicBg,
			&st::historyPeer8UserpicBg2,
		} },
	};
	if (const auto i = badges.find(privacy); i != end(badges)) {
		return i->second;
	}
	return {};
}

UserpicBadge::UserpicBadge(
	not_null<QWidget*> userpic,
	PrivacyBadge badge,
	Fn<void()> clicked)
: RpWidget(userpic->parentWidget())
, _userpic(userpic)
, _badgeData(badge)
, _clickable(std::make_unique<Ui::AbstractButton>(parentWidget())) {
	_clickable->setClickedCallback(std::move(clicked));
	userpic->installEventFilter(this);
	updateGeometry();
	setAttribute(Qt::WA_TransparentForMouseEvents);
	Ui::PostponeCall(this, [=] {
		_userpic->raise();
	});
	show();
}

bool UserpicBadge::eventFilter(QObject *o, QEvent *e) {
	if (o != _userpic) {
		return false;
	}
	const auto type = e->type();
	switch (type) {
	case QEvent::Move:
	case QEvent::Resize:
		updateGeometry();
		return false;
	case QEvent::Paint:
		return !_grabbing;
	}
	return false;
}

void UserpicBadge::paintEvent(QPaintEvent *e) {
	const auto ratio = style::DevicePixelRatio();
	const auto layerSize = size() * ratio;
	if (_layer.size() != layerSize) {
		_layer = QImage(layerSize, QImage::Format_ARGB32_Premultiplied);
		_layer.setDevicePixelRatio(ratio);
	}
	_layer.fill(Qt::transparent);
	auto q = QPainter(&_layer);

	_grabbing = true;
	Ui::RenderWidget(q, _userpic);
	_grabbing = false;

	auto hq = PainterHighQualityEnabler(q);
	auto pen = st::transparent->p;
	pen.setWidthF(st::storiesBadgeOutline);
	const auto half = st::storiesBadgeOutline / 2.;
	auto outer = QRectF(_badge).marginsAdded({ half, half, half, half });
	auto gradient = QLinearGradient(outer.topLeft(), outer.bottomLeft());
	gradient.setStops({
		{ 0., (*_badgeData.bg1)->c },
		{ 1., (*_badgeData.bg2)->c },
	});
	q.setPen(pen);
	q.setBrush(gradient);
	q.setCompositionMode(QPainter::CompositionMode_Source);
	q.drawEllipse(outer);
	q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	_badgeData.icon->paintInCenter(q, _badge);
	q.end();

	QPainter(this).drawImage(0, 0, _layer);
}

void UserpicBadge::updateGeometry() {
	const auto width = _userpic->width() + st::storiesBadgeShift.x();
	const auto height = _userpic->height() + st::storiesBadgeShift.y();
	setGeometry(QRect(_userpic->pos(), QSize{ width, height }));
	const auto inner = QRect(QPoint(), _badgeData.icon->size());
	const auto badge = inner.marginsAdded(st::storiesBadgePadding).size();
	_badge = QRect(
		QPoint(width - badge.width(), height - badge.height()),
		badge);
	_clickable->setGeometry(_badge.translated(pos()));
	update();
}

[[nodiscard]] std::unique_ptr<Ui::RpWidget> MakePrivacyBadge(
		not_null<QWidget*> userpic,
		Data::StoryPrivacy privacy,
		Fn<void()> clicked) {
	const auto badge = LookupPrivacyBadge(privacy);
	if (!badge.icon) {
		return nullptr;
	}
	return std::make_unique<UserpicBadge>(
		userpic,
		badge,
		std::move(clicked));
}

[[nodiscard]] Timestamp ComposeTimestamp(TimeId when, TimeId now) {
	const auto minutes = (now - when) / 60;
	if (!minutes) {
		return { tr::lng_mediaview_just_now(tr::now), 61 - (now - when) };
	} else if (minutes < 60) {
		return {
			tr::lng_mediaview_minutes_ago(tr::now, lt_count, minutes),
			61 - ((now - when) % 60),
		};
	}
	const auto hours = (now - when) / 3600;
	if (hours < 12) {
		return {
			tr::lng_mediaview_hours_ago(tr::now, lt_count, hours),
			3601 - ((now - when) % 3600),
		};
	}
	const auto whenFull = base::unixtime::parse(when);
	const auto nowFull = base::unixtime::parse(now);
	const auto locale = QLocale();
	auto tomorrow = nowFull;
	tomorrow.setDate(nowFull.date().addDays(1));
	tomorrow.setTime(QTime(0, 0, 1));
	const auto seconds = int(nowFull.secsTo(tomorrow));
	if (whenFull.date() == nowFull.date()) {
		const auto whenTime = locale.toString(
			whenFull.time(),
			QLocale::ShortFormat);
		return {
			tr::lng_mediaview_today(tr::now, lt_time, whenTime),
			seconds,
		};
	} else if (whenFull.date().addDays(1) == nowFull.date()) {
		const auto whenTime = locale.toString(
			whenFull.time(),
			QLocale::ShortFormat);
		return {
			tr::lng_mediaview_yesterday(tr::now, lt_time, whenTime),
			seconds,
		};
	}
	return { Ui::FormatDateTime(whenFull) };
}

[[nodiscard]] TextWithEntities ComposeName(HeaderData data) {
	auto result = Ui::Text::Bold(data.user->shortName());
	if (data.fullCount) {
		result.append(QString::fromUtf8(" \xE2\x80\xA2 %1/%2"
		).arg(data.fullIndex + 1
		).arg(data.fullCount));
	}
	return result;
}

[[nodiscard]] Timestamp ComposeDetails(HeaderData data, TimeId now) {
	auto result = ComposeTimestamp(data.date, now);
	if (data.edited) {
		result.text.append(
			QString::fromUtf8(" \xE2\x80\xA2 ") + tr::lng_edited(tr::now));
	}
	return result;
}

} // namespace

Header::Header(not_null<Controller*> controller)
: _controller(controller)
, _dateUpdateTimer([=] { updateDateText(); }) {
}

Header::~Header() = default;

void Header::show(HeaderData data) {
	if (_data == data) {
		return;
	}
	const auto userChanged = !_data
		|| (_data->user != data.user);
	const auto nameDataChanged = userChanged
		|| !_name
		|| (_data->fullCount != data.fullCount)
		|| (data.fullCount && _data->fullIndex != data.fullIndex);
	_data = data;
	if (userChanged) {
		_date = nullptr;
		_name = nullptr;
		_userpic = nullptr;
		_info = nullptr;
		_privacy = nullptr;
		const auto parent = _controller->wrap();
		auto widget = std::make_unique<Ui::RpWidget>(parent);
		const auto raw = widget.get();
		_info = std::make_unique<Ui::AbstractButton>(raw);
		_info->setClickedCallback([=] {
			_controller->uiShow()->show(PrepareShortInfoBox(_data->user));
		});
		_userpic = std::make_unique<Ui::UserpicButton>(
			raw,
			data.user,
			st::storiesHeaderPhoto);
		_userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		_userpic->show();
		_userpic->move(
			st::storiesHeaderMargin.left(),
			st::storiesHeaderMargin.top());
		raw->show();
		_widget = std::move(widget);

		_controller->layoutValue(
		) | rpl::start_with_next([=](const Layout &layout) {
			raw->setGeometry(layout.header);
		}, raw->lifetime());
	}
	if (nameDataChanged) {
		_name = std::make_unique<Ui::FlatLabel>(
			_widget.get(),
			rpl::single(ComposeName(data)),
			st::storiesHeaderName);
		_name->setAttribute(Qt::WA_TransparentForMouseEvents);
		_name->setOpacity(kNameOpacity);
		_name->move(st::storiesHeaderNamePosition);
		_name->show();

		rpl::combine(
			_name->widthValue(),
			_widget->heightValue()
		) | rpl::start_with_next([=](int width, int height) {
			if (_date) {
				_info->setGeometry(
					{ 0, 0, std::max(width, _date->width()), height });
			}
		}, _name->lifetime());
	}
	auto timestamp = ComposeDetails(data, base::unixtime::now());
	_date = std::make_unique<Ui::FlatLabel>(
		_widget.get(),
		std::move(timestamp.text),
		st::storiesHeaderDate);
	_date->setAttribute(Qt::WA_TransparentForMouseEvents);
	_date->setOpacity(kDateOpacity);
	_date->show();
	_date->move(st::storiesHeaderDatePosition);

	_date->widthValue(
	) | rpl::start_with_next([=](int width) {
		_info->setGeometry(
			{ 0, 0, std::max(width, _name->width()), _widget->height() });
	}, _name->lifetime());

	_privacy = MakePrivacyBadge(_userpic.get(), data.privacy, [=] {

	});

	if (timestamp.changes > 0) {
		_dateUpdateTimer.callOnce(timestamp.changes * crl::time(1000));
	}
}

void Header::raise() {
	if (_widget) {
		_widget->raise();
	}
}

void Header::updateDateText() {
	if (!_date || !_data || !_data->date) {
		return;
	}
	auto timestamp = ComposeDetails(*_data, base::unixtime::now());
	_date->setText(timestamp.text);
	if (timestamp.changes > 0) {
		_dateUpdateTimer.callOnce(timestamp.changes * crl::time(1000));
	}
}

} // namespace Media::Stories
