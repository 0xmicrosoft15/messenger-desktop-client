/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_premium.h"

#include "base/random.h"
#include "core/application.h"
#include "info/info_wrap_widget.h" // Info::Wrap.
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_premium.h"
#include "ui/abstract_button.h"
#include "ui/basic_click_handlers.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "window/window_session_controller.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QSvgRenderer>

namespace Settings {
namespace {

using SectionCustomTopBarData = Info::Settings::SectionCustomTopBarData;

constexpr auto kBodyAnimationPart = 0.90;
constexpr auto kTitleAnimationPart = 0.15;

constexpr auto kTitleAdditionalScale = 0.15;

struct Entry {
	const style::icon *icon;
	rpl::producer<QString> title;
	rpl::producer<QString> description;
};

using Order = std::vector<QString>;

[[nodiscard]] Order FallbackOrder() {
	return Order{
		QString("double_limits"),
		QString("more_upload"),
		QString("faster_download"),
		QString("voice_to_text"),
		QString("no_ads"),
		QString("unique_reactions"),
		QString("premium_stickers"),
		QString("advanced_chat_management"),
		QString("profile_badge"),
		QString("animated_userpics"),
	};
}

[[nodiscard]] base::flat_map<QString, Entry> EntryMap() {
	return base::flat_map<QString, Entry>{
		{
			QString("double_limits"),
			Entry{
				&st::settingsPremiumIconDouble,
				tr::lng_premium_summary_subtitle_double_limits(),
				tr::lng_premium_summary_about_double_limits(),
			},
		},
		{
			QString("more_upload"),
			Entry{
				&st::settingsPremiumIconFiles,
				tr::lng_premium_summary_subtitle_more_upload(),
				tr::lng_premium_summary_about_more_upload(),
			},
		},
		{
			QString("faster_download"),
			Entry{
				&st::settingsPremiumIconSpeed,
				tr::lng_premium_summary_subtitle_faster_download(),
				tr::lng_premium_summary_about_faster_download(),
			},
		},
		{
			QString("voice_to_text"),
			Entry{
				&st::settingsPremiumIconVoice,
				tr::lng_premium_summary_subtitle_voice_to_text(),
				tr::lng_premium_summary_about_voice_to_text(),
			},
		},
		{
			QString("no_ads"),
			Entry{
				&st::settingsPremiumIconChannelsOff,
				tr::lng_premium_summary_subtitle_no_ads(),
				tr::lng_premium_summary_about_no_ads(),
			},
		},
		{
			QString("unique_reactions"),
			Entry{
				&st::settingsPremiumIconLike,
				tr::lng_premium_summary_subtitle_unique_reactions(),
				tr::lng_premium_summary_about_unique_reactions(),
			},
		},
		{
			QString("premium_stickers"),
			Entry{
				&st::settingsIconStickers,
				tr::lng_premium_summary_subtitle_premium_stickers(),
				tr::lng_premium_summary_about_premium_stickers(),
			},
		},
		{
			QString("advanced_chat_management"),
			Entry{
				&st::settingsIconChat,
				tr::lng_premium_summary_subtitle_advanced_chat_management(),
				tr::lng_premium_summary_about_advanced_chat_management(),
			},
		},
		{
			QString("profile_badge"),
			Entry{
				&st::settingsPremiumIconStar,
				tr::lng_premium_summary_subtitle_profile_badge(),
				tr::lng_premium_summary_about_profile_badge(),
			},
		},
		{
			QString("animated_userpics"),
			Entry{
				&st::settingsPremiumIconPlay,
				tr::lng_premium_summary_subtitle_animated_userpics(),
				tr::lng_premium_summary_about_animated_userpics(),
			},
		},
	};
}

void SendAppLog(
		not_null<Main::Session*> session,
		const QString &type,
		const MTPJSONValue &data) {
	const auto now = double(base::unixtime::now())
		+ (QTime::currentTime().msec() / 1000.);
	session->api().request(MTPhelp_SaveAppLog(
		MTP_vector<MTPInputAppEvent>(1, MTP_inputAppEvent(
			MTP_double(now),
			MTP_string(type),
			MTP_long(0),
			data
		))
	)).send();
}

[[nodiscard]] QString ResolveRef(const QString &ref) {
	return ref.isEmpty() ? "settings" : ref;
}

void SendScreenShow(
		not_null<Window::SessionController*> controller,
		const std::vector<QString> &order,
		const QString &ref) {
	auto list = QVector<MTPJSONValue>();
	list.reserve(order.size());
	for (const auto &element : order) {
		list.push_back(MTP_jsonString(MTP_string(element)));
	}
	auto values = QVector<MTPJSONObjectValue>{
		MTP_jsonObjectValue(
			MTP_string("premium_promo_order"),
			MTP_jsonArray(MTP_vector<MTPJSONValue>(std::move(list)))),
		MTP_jsonObjectValue(
			MTP_string("source"),
			MTP_jsonString(MTP_string(ResolveRef(ref)))),
	};
	const auto data = MTP_jsonObject(
		MTP_vector<MTPJSONObjectValue>(std::move(values)));
	SendAppLog(
		&controller->session(),
		"premium.promo_screen_show",
		data);
}

void SendScreenAccept(not_null<Window::SessionController*> controller) {
	SendAppLog(
		&controller->session(),
		"premium.promo_screen_accept",
		MTP_jsonNull());
}

class MiniStars final {
public:
	MiniStars(Fn<void()> updateCallback);

	void paint(Painter &p, const QRectF &rect);

private:
	struct MiniStar {
		crl::time birthTime = 0;
		crl::time deathTime = 0;
		int angle = 0;
		float64 size = 0.;
		float64 alpha = 0.;
	};

	struct Interval {
		int from = 0;
		int length = 0;
	};

	void createStar(crl::time now);
	[[nodiscard]] int angle() const;
	[[nodiscard]] crl::time timeNow() const;
	[[nodiscard]] int randomInterval(const Interval &interval) const;

	const std::vector<Interval> _availableAngles;
	const Interval _lifeLength;
	const Interval _deathTime;
	const Interval _size;
	const Interval _alpha;

	const float64 _appearProgressTill;
	const float64 _disappearProgressAfter;
	const float64 _distanceProgressStart;

	QSvgRenderer _sprite;

	Ui::Animations::Basic _animation;

	std::vector<MiniStar> _ministars;

	crl::time _nextBirthTime = 0;

};

MiniStars::MiniStars(Fn<void()> updateCallback)
: _availableAngles({
	Interval{ -10, 40 },
	Interval{ 180 + 10 - 40, 40 },
	Interval{ 180 + 15, 50 },
	Interval{ -15 - 50, 50 },
})
, _lifeLength({ 150, 200 })
, _deathTime({ 1500, 2000 })
, _size({ 10, 20 })
, _alpha({ 40, 60 })
, _appearProgressTill(0.2)
, _disappearProgressAfter(0.8)
, _distanceProgressStart(0.5)
, _sprite(u":/gui/icons/settings/starmini.svg"_q)
, _animation([=](crl::time now) {
	if (now > _nextBirthTime) {
		createStar(now);
		_nextBirthTime = now + randomInterval(_lifeLength);
	}
	updateCallback();
}) {
	if (anim::Disabled()) {
		const auto from = _deathTime.from + _deathTime.length;
		for (auto i = -from; i < 0; i += randomInterval(_lifeLength)) {
			createStar(i);
		}
		updateCallback();
	} else {
		_animation.start();
	}
}

int MiniStars::randomInterval(const Interval &interval) const {
	return interval.from + base::RandomIndex(interval.length);
}

crl::time MiniStars::timeNow() const {
	return anim::Disabled() ? 0 : crl::now();
}

void MiniStars::paint(Painter &p, const QRectF &rect) {
	const auto center = rect.center();
	const auto opacity = p.opacity();
	for (const auto &ministar : _ministars) {
		const auto progress = (timeNow() - ministar.birthTime)
			/ float64(ministar.deathTime - ministar.birthTime);
		if (progress > 1.) {
			continue;
		}
		const auto appearProgress = std::clamp(
			progress / _appearProgressTill,
			0.,
			1.);
		const auto rsin = float(std::sin(ministar.angle * M_PI / 180.));
		const auto rcos = float(std::cos(ministar.angle * M_PI / 180.));
		const auto end = QPointF(
			rect.width() / 1.5 * rcos,
			rect.height() / 1.5 * rsin);

		const auto alphaProgress = 1.
			- (std::clamp(progress - _disappearProgressAfter, 0., 1.)
				/ (1. - _disappearProgressAfter));
		p.setOpacity(ministar.alpha * alphaProgress * appearProgress);

		const auto distanceProgress = _distanceProgressStart + progress;
		const auto starSize = ministar.size * appearProgress;
		_sprite.render(&p, QRectF(
			center.x()
				+ anim::interpolateF(0, end.x(), distanceProgress)
				- starSize / 2.,
			center.y()
				+ anim::interpolateF(0, end.y(), distanceProgress)
				- starSize / 2.,
			starSize,
			starSize));
	}
	p.setOpacity(opacity);
}

int MiniStars::angle() const {
	const auto &interval = _availableAngles[
		base::RandomIndex(_availableAngles.size())];
	return base::RandomIndex(interval.length) + interval.from;
}

void MiniStars::createStar(crl::time now) {
	auto ministar = MiniStar{
		.birthTime = now,
		.deathTime = now + randomInterval(_deathTime),
		.angle = angle(),
		.size = float64(randomInterval(_size)),
		.alpha = float64(randomInterval(_alpha)) / 100.,
	};
	for (auto i = 0; i < _ministars.size(); i++) {
		if (ministar.birthTime > _ministars[i].deathTime) {
			_ministars[i] = ministar;
			return;
		}
	}
	_ministars.push_back(ministar);
}

class TopBar final : public Ui::RpWidget {
public:
	TopBar(not_null<QWidget*> parent);

	void setRoundEdges(bool value);
	void setTextPosition(int x, int y);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::font &_titleFont;
	const style::margins &_titlePadding;
	const style::TextStyle &_aboutSt;
	MiniStars _ministars;
	QSvgRenderer _star;
	Ui::Text::String _about;

	QPoint _titlePosition;
	QPainterPath _titlePath;
	bool _roundEdges = true;

};

TopBar::TopBar(not_null<QWidget*> parent)
: Ui::RpWidget(parent)
, _titleFont(st::boxTitle.style.font)
, _titlePadding(st::settingsPremiumTitlePadding)
, _aboutSt(st::settingsPremiumAboutTextStyle)
, _ministars([=] { update(); })
, _star(u":/gui/icons/settings/star.svg"_q) {
	_titlePath.addText(
		0,
		_titleFont->ascent,
		_titleFont,
		tr::lng_premium_summary_title(tr::now));
	_about.setMarkedText(
		_aboutSt,
		tr::lng_premium_summary_top_about(tr::now, Ui::Text::RichLangValue));
}

void TopBar::setRoundEdges(bool value) {
	_roundEdges = value;
	update();
}

void TopBar::setTextPosition(int x, int y) {
	_titlePosition = { x, y };
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), Qt::transparent);

	const auto progress = (height() - minimumHeight())
		/ float64(maximumHeight() - minimumHeight());
	const auto topProgress = 1. -
		std::clamp(
			(1. - progress) / kBodyAnimationPart,
			0.,
			1.);
	const auto bodyProgress = topProgress;

	const auto r = rect();
	auto pathTop = QPainterPath();
	if (_roundEdges) {
		pathTop.addRoundedRect(r, st::boxRadius, st::boxRadius);
	} else {
		pathTop.addRect(r);
	}
	auto pathBottom = QPainterPath();
	pathBottom.addRect(
		QRect(
			QPoint(r.x(), r.y() + r.height() - st::boxRadius),
			QSize(r.width(), st::boxRadius)));

	const auto gradientPointTop = r.height() / 3. * 2.;
	auto gradient = QLinearGradient(
		QPointF(0, gradientPointTop),
		QPointF(r.width(), r.height() - gradientPointTop));
	gradient.setColorAt(0., st::premiumButtonBg1->c);
	gradient.setColorAt(.6, st::premiumButtonBg2->c);
	gradient.setColorAt(1., st::premiumButtonBg3->c);

	PainterHighQualityEnabler hq(p);
	p.fillPath(pathTop + pathBottom, gradient);

	p.setOpacity(bodyProgress);

	const auto starRect = [&](float64 topProgress, float64 sizeProgress) {
		const auto starSize = st::settingsPremiumStarSize * sizeProgress;
		return QRectF(
			QPointF(
				(width() - starSize.width()) / 2,
				st::settingsPremiumStarTopSkip * topProgress),
			starSize);
	};
	const auto currentStarRect = starRect(topProgress, bodyProgress);

	p.translate(currentStarRect.center());
	p.scale(bodyProgress, bodyProgress);
	p.translate(-currentStarRect.center());
	_ministars.paint(p, starRect(topProgress, 1.));
	p.resetTransform();

	_star.render(&p, currentStarRect);

	p.setPen(st::premiumButtonFg);

	const auto &padding = st::boxRowPadding;
	const auto availableWidth = width() - padding.left() - padding.right();
	const auto titleTop = currentStarRect.top()
		+ currentStarRect.height()
		+ _titlePadding.top();
	const auto titlePathRect = _titlePath.boundingRect();
	const auto aboutTop = titleTop
		+ titlePathRect.height()
		+ _titlePadding.bottom();

	p.setFont(_aboutSt.font);
	_about.draw(p, padding.left(), aboutTop, availableWidth, style::al_top);

	// Title.
	p.setOpacity(1.);
	p.setFont(_titleFont);
	const auto titleProgress = 1. - progress;
	const auto fullStarRect = starRect(1., 1.);
	const auto fullTitleTop = fullStarRect.top()
		+ fullStarRect.height()
		+ _titlePadding.top();
	p.translate(
		anim::interpolate(
			(width() - titlePathRect.width()) / 2,
			_titlePosition.x(),
			titleProgress),
		anim::interpolate(fullTitleTop, _titlePosition.y(), titleProgress));

	const auto scale = 1. + kTitleAdditionalScale * (1. - titleProgress);
	p.translate(titlePathRect.center());
	p.scale(scale, scale);
	p.translate(-titlePathRect.center());
	p.fillPath(_titlePath, st::premiumButtonFg);
}

class Premium : public Section<Premium> {
public:
	Premium(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;
	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

	[[nodiscard]] bool hasFlexibleTopBar() const override;

	void setStepDataReference(std::any &data) override;

	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;
	const QString _ref;

	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;

	rpl::event_stream<> _showBack;

};

Premium::Premium(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _ref(ResolveRef(controller->premiumRef())) {
	setupContent();
}

rpl::producer<QString> Premium::title() {
	return tr::lng_premium_summary_title();
}

bool Premium::hasFlexibleTopBar() const {
	return true;
}

rpl::producer<> Premium::sectionShowBack() {
	return _showBack.events();
}

void Premium::setStepDataReference(std::any &data) {
	const auto my = std::any_cast<SectionCustomTopBarData>(&data);
	if (my) {
		_backToggles = std::move(
			my->backButtonEnables
		) | rpl::map_to(true);
		_wrap = std::move(my->wrapValue);
	}
}

void Premium::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto &stDefault = st::settingsButton;
	const auto &stLabel = st::defaultFlatLabel;
	const auto iconSize = st::settingsPremiumIconDouble.size();
	const auto &titlePadding = st::settingsPremiumRowTitlePadding;
	const auto &descriptionPadding = st::settingsPremiumRowAboutPadding;

	AddSkip(content, stDefault.padding.top() + titlePadding.top());

	auto entryMap = EntryMap();
	auto iconContainers = std::vector<Ui::AbstractButton*>();
	iconContainers.reserve(int(entryMap.size()));

	const auto addRow = [&](
			rpl::producer<QString> &&title,
			rpl::producer<QString> &&text) {
		const auto labelAscent = stLabel.style.font->ascent;

		const auto label = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(title) | rpl::map(Ui::Text::Bold),
				stLabel),
			titlePadding);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(text),
				st::boxDividerLabel),
			descriptionPadding);

		const auto dummy = Ui::CreateChild<Ui::AbstractButton>(content);
		dummy->setAttribute(Qt::WA_TransparentForMouseEvents);

		content->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			dummy->resize(s.width(), iconSize.height());
		}, dummy->lifetime());

		label->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			dummy->moveToLeft(0, r.y() + (r.height() - labelAscent));
		}, dummy->lifetime());

		iconContainers.push_back(dummy);
	};

	auto icons = std::vector<const style::icon *>();
	icons.reserve(int(entryMap.size()));
	{
		const auto &account = _controller->session().account();
		const auto mtpOrder = account.appConfig().get<Order>(
			"premium_promo_order",
			FallbackOrder());
		const auto processEntry = [&](Entry &entry) {
			icons.push_back(entry.icon);
			addRow(base::take(entry.title), base::take(entry.description));
		};

		for (const auto &key : mtpOrder) {
			auto it = entryMap.find(key);
			if (it == end(entryMap)) {
				continue;
			}
			processEntry(it->second);
		}

		SendScreenShow(_controller, mtpOrder, _ref);
	}

	content->resizeToWidth(content->height());

	// Icons.
	Assert(iconContainers.size() > 2);
	const auto from = iconContainers.front()->y();
	const auto to = iconContainers.back()->y() + iconSize.height();
	auto gradient = QLinearGradient(0, 0, 0, to - from);
	gradient.setColorAt(0.0, st::premiumIconBg1->c);
	gradient.setColorAt(.28, st::premiumIconBg2->c);
	gradient.setColorAt(.55, st::premiumButtonBg2->c);
	gradient.setColorAt(1.0, st::premiumButtonBg1->c);
	for (auto i = 0; i < int(icons.size()); i++) {
		const auto &iconContainer = iconContainers[i];

		const auto pointTop = iconContainer->y() - from;
		const auto pointBottom = pointTop + iconContainer->height();
		const auto ratioTop = pointTop / float64(to - from);
		const auto ratioBottom = pointBottom / float64(to - from);

		auto resultGradient = QLinearGradient(
			QPointF(),
			QPointF(0, pointBottom - pointTop));

		resultGradient.setColorAt(
			.0,
			anim::gradient_color_at(gradient, ratioTop));
		resultGradient.setColorAt(
			.1,
			anim::gradient_color_at(gradient, ratioBottom));

		const auto brush = QBrush(resultGradient);
		AddButtonIcon(
			iconContainer,
			stDefault,
			{ .icon = icons[i], .backgroundBrush = brush });
	}

	AddSkip(content, descriptionPadding.bottom());
	AddSkip(content);
	AddDivider(content);
	AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_premium_summary_bottom_subtitle(
			) | rpl::map(Ui::Text::Bold),
			stLabel),
		st::settingsSubsectionTitlePadding);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_premium_summary_bottom_about(Ui::Text::RichLangValue),
			st::aboutLabel),
		st::boxRowPadding);
	AddSkip(content, stDefault.padding.top() + stDefault.padding.bottom());

	Ui::ResizeFitChild(this, content);

}

QPointer<Ui::RpWidget> Premium::createPinnedToTop(
		not_null<QWidget*> parent) {
	const auto content = Ui::CreateChild<TopBar>(parent.get());

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		content->setRoundEdges(wrap == Info::Wrap::Layer);
	}, content->lifetime());

	content->setMaximumHeight(st::introQrStepsTop);
	content->setMinimumHeight(st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			content,
			object_ptr<Ui::IconButton>(
				content,
				isLayer
					? st::settingsPremiumLayerTopBarBack
					: st::settingsPremiumTopBarBack),
			st::infoTopBarScale);
		_back->setDuration(0);
		_back->toggleOn(_backToggles.value());
		_back->entity()->addClickHandler([=] {
			_showBack.fire({});
		});
		_back->toggledValue(
		) | rpl::start_with_next([=](bool toggled) {
			const auto &st = isLayer ? st::infoLayerTopBar : st::infoTopBar;
			content->setTextPosition(
				toggled ? st.back.width : st.titlePosition.x(),
				st.titlePosition.y());
		}, _back->lifetime());

		if (!isLayer) {
			_close = nullptr;
		} else {
			_close = base::make_unique_q<Ui::IconButton>(
				content,
				st::settingsPremiumTopBarClose);
			_close->addClickHandler([=] {
				_controller->parentController()->hideLayer();
				_controller->parentController()->hideSpecialLayer();
			});
			content->widthValue(
			) | rpl::start_with_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}
	}, content->lifetime());

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

QPointer<Ui::RpWidget> Premium::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(parent.get());

	auto result = object_ptr<Ui::GradientButton>(
		content,
		Ui::Premium::ButtonGradientStops());

	result->setClickedCallback([=] {
		SendScreenAccept(_controller);
		StartPremiumPayment(_controller, _ref);
	});

	const auto &st = st::premiumPreviewBox.button;
	result->resize(content->width(), st.height);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result.data(),
		tr::lng_premium_summary_button(tr::now, lt_cost, "$5"),
		st::premiumPreviewButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		result->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=](int outer, int width) {
		label->moveToLeft(
			(outer - width) / 2,
			st::premiumPreviewBox.button.textTop,
			outer);
	}, label->lifetime());
	content->add(std::move(result), st::settingsPremiumButtonPadding);

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

} // namespace

Type PremiumId() {
	return Premium::Id();
}

void ShowPremium(not_null<Main::Session*> session, const QString &ref) {
	const auto active = Core::App().activeWindow();
	const auto controller = (active && active->isPrimary())
		? active->sessionController()
		: nullptr;
	if (controller && session == &controller->session()) {
		ShowPremium(controller, ref);
	} else {
		for (const auto &controller : session->windows()) {
			if (controller->window().isPrimary()) {
				ShowPremium(controller, ref);
			}
		}
	}
}

void ShowPremium(
		not_null<Window::SessionController*> controller,
		const QString &ref) {
	controller->setPremiumRef(ref);
	controller->showSettings(Settings::PremiumId());
}

void StartPremiumPayment(
		not_null<Window::SessionController*> controller,
		const QString &ref) {
	const auto account = &controller->session().account();
	const auto username = account->appConfig().get<QString>(
		"premium_bot_username",
		QString());
	const auto slug = account->appConfig().get<QString>(
		"premium_invoice_slug",
		QString());
	if (!username.isEmpty()) {
		controller->showPeerByLink(Window::SessionNavigation::PeerByLinkInfo{
			.usernameOrId = username,
			.resolveType = Window::ResolveType::BotStart,
			.startToken = ref,
			.startAutoSubmit = true,
		});
	} else if (!slug.isEmpty()) {
		UrlClickHandler::Open("https://t.me/$" + slug);
	}
}

} // namespace Settings
