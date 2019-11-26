/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_qr.h"

#include "intro/introphone.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/effects/radial_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "main/main_account.h"
#include "boxes/confirm_box.h"
#include "core/application.h"
#include "base/unixtime.h"
#include "qr/qr_generate.h"
#include "styles/style_intro.h"

namespace Intro {
namespace details {
namespace {

[[nodiscard]] QImage TelegramLogoImage() {
	const auto size = QSize(st::introQrCenterSize, st::introQrCenterSize);
	auto result = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::activeButtonBg);
		p.setPen(Qt::NoPen);
		p.drawEllipse(QRect(QPoint(), size));
		st::introQrPlane.paintInCenter(p, QRect(QPoint(), size));
	}
	return result;
}

[[nodiscard]] QImage TelegramQrExact(const Qr::Data &data, int pixel) {
	return Qr::Generate(data, pixel, st::windowFg->c);
}

[[nodiscard]] QImage TelegramQr(const Qr::Data &data, int pixel, int max = 0) {
	Expects(data.size > 0);

	if (max > 0 && data.size * pixel > max) {
		pixel = std::max(max / data.size, 1);
	}
	const auto qr = TelegramQrExact(data, pixel * style::DevicePixelRatio());
	auto result = QImage(qr.size(), QImage::Format_ARGB32_Premultiplied);
	result.fill(st::windowBg->c);
	{
		auto p = QPainter(&result);
		p.drawImage(QRect(QPoint(), qr.size()), qr);
	}
	return result;
}

[[nodiscard]] not_null<Ui::RpWidget*> PrepareQrWidget(
		not_null<QWidget*> parent,
		rpl::producer<QByteArray> codes) {
	struct State {
		explicit State(Fn<void()> callback)
		: waiting(callback, st::defaultInfiniteRadialAnimation) {
		}

		QImage previous;
		QImage qr;
		QImage center;
		Ui::Animations::Simple shown;
		Ui::InfiniteRadialAnimation waiting;
	};
	auto qrs = std::move(
		codes
	) | rpl::map([](const QByteArray &code) {
		return Qr::Encode(code, Qr::Redundancy::Quartile);
	});
	auto palettes = rpl::single(
		rpl::empty_value()
	) | rpl::then(
		style::PaletteChanged()
	);
	auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto state = result->lifetime().make_state<State>(
		[=] { result->update(); });
	state->waiting.start();
	result->resize(st::introQrMaxSize, st::introQrMaxSize);
	rpl::combine(
		std::move(qrs),
		rpl::duplicate(palettes)
	) | rpl::map([](const Qr::Data &code, const auto &) {
		return TelegramQr(code, st::introQrPixel, st::introQrMaxSize);
	}) | rpl::start_with_next([=](QImage &&image) {
		state->previous = std::move(state->qr);
		state->qr = std::move(image);
		state->waiting.stop();
		state->shown.stop();
		state->shown.start(
			[=] { result->update(); },
			0.,
			1.,
			st::fadeWrapDuration);
	}, result->lifetime());
	std::move(
		palettes
	) | rpl::map([] {
		return TelegramLogoImage();
	}) | rpl::start_with_next([=](QImage &&image) {
		state->center = std::move(image);
	}, result->lifetime());
	result->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(result);
		const auto shown = state->qr.isNull() ? 0. : state->shown.value(1.);
		if (!state->qr.isNull()) {
			const auto size = state->qr.size() / cIntRetinaFactor();
			const auto qr = QRect(
				(result->width() - size.width()) / 2,
				(result->height() - size.height()) / 2,
				size.width(),
				size.height());
			if (shown == 1.) {
				state->previous = QImage();
			} else if (!state->previous.isNull()) {
				p.drawImage(qr, state->previous);
			}
			p.setOpacity(shown);
			p.drawImage(qr, state->qr);
			p.setOpacity(1.);
		}
		const auto rect = QRect(
			(result->width() - st::introQrCenterSize) / 2,
			(result->height() - st::introQrCenterSize) / 2,
			st::introQrCenterSize,
			st::introQrCenterSize);
		p.drawImage(rect, state->center);
		if (!anim::Disabled() && state->waiting.animating()) {
			auto hq = PainterHighQualityEnabler(p);
			const auto line = st::radialLine;
			const auto radial = state->waiting.computeState();
			auto pen = st::activeButtonBg->p;
			pen.setWidth(line);
			pen.setCapStyle(Qt::RoundCap);
			p.setOpacity(radial.shown * (1. - shown));
			p.setPen(pen);
			p.drawArc(
				rect.marginsAdded({ line, line, line, line }),
				radial.arcFrom,
				radial.arcLength);
			p.setOpacity(1.);
		}
	}, result->lifetime());
	return result;
}

} // namespace

QrWidget::QrWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _refreshTimer([=] { refreshCode(); }) {
	setTitleText(rpl::single(QString()));
	setDescriptionText(rpl::single(QString()));
	setErrorCentered(true);

	account->destroyStaleAuthorizationKeys();
	account->mtpUpdates(
	) | rpl::start_with_next([=](const MTPUpdates &updates) {
		checkForTokenUpdate(updates);
	}, lifetime());

	setupControls();
	refreshCode();
}

int QrWidget::errorTop() const {
	return contentTop() + st::introQrErrorTop;
}

void QrWidget::checkForTokenUpdate(const MTPUpdates &updates) {
	updates.match([&](const MTPDupdateShort &data) {
		checkForTokenUpdate(data.vupdate());
	}, [&](const MTPDupdates &data) {
		for (const auto &update : data.vupdates().v) {
			checkForTokenUpdate(update);
		}
	}, [&](const MTPDupdatesCombined &data) {
		for (const auto &update : data.vupdates().v) {
			checkForTokenUpdate(update);
		}
	}, [](const auto &) {});
}

void QrWidget::checkForTokenUpdate(const MTPUpdate &update) {
	update.match([&](const MTPDupdateLoginToken &data) {
		if (_requestId) {
			_forceRefresh = true;
		} else {
			_refreshTimer.cancel();
			refreshCode();
		}
	}, [](const auto &) {});
}

void QrWidget::submit() {
	goReplace<PhoneWidget>();
}

rpl::producer<QString> QrWidget::nextButtonText() const {
	return rpl::single(QString());
}

void QrWidget::setupControls() {
	const auto code = PrepareQrWidget(this, _qrCodes.events());
	rpl::combine(
		sizeValue(),
		code->widthValue()
	) | rpl::start_with_next([=](QSize size, int codeWidth) {
		code->moveToLeft(
			(size.width() - codeWidth) / 2,
			contentTop() + st::introQrTop);
	}, code->lifetime());

	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_intro_qr_title(),
		st::introQrTitle);
	rpl::combine(
		sizeValue(),
		title->widthValue()
	) | rpl::start_with_next([=](QSize size, int titleWidth) {
		title->moveToLeft(
			(size.width() - st::introQrLabelsWidth) / 2,
			contentTop() + st::introQrTitleTop);
	}, title->lifetime());

	const auto steps = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto texts = {
		tr::lng_intro_qr_step1,
		tr::lng_intro_qr_step2,
		tr::lng_intro_qr_step3,
	};
	for (const auto &text : texts) {
		steps->add(
			object_ptr<Ui::FlatLabel>(
				this,
				text(Ui::Text::RichLangValue),
				st::introQrStep),
			st::introQrStepMargins);
	}
	steps->resizeToWidth(st::introQrLabelsWidth);
	rpl::combine(
		sizeValue(),
		steps->widthValue()
	) | rpl::start_with_next([=](QSize size, int stepsWidth) {
		steps->moveToLeft(
			(size.width() - stepsWidth) / 2,
			contentTop() + st::introQrStepsTop);
	}, steps->lifetime());

	const auto skip = Ui::CreateChild<Ui::LinkButton>(
		this,
		tr::lng_intro_qr_skip(tr::now));
	rpl::combine(
		sizeValue(),
		skip->widthValue()
	) | rpl::start_with_next([=](QSize size, int skipWidth) {
		skip->moveToLeft(
			(size.width() - skipWidth) / 2,
			contentTop() + st::introQrSkipTop);
	}, skip->lifetime());

	skip->setClickedCallback([=] { submit(); });
}

void QrWidget::refreshCode() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPauth_ExportLoginToken(
		MTP_int(ApiId),
		MTP_string(ApiHash),
		MTP_vector<MTPint>(0)
	)).done([=](const MTPauth_LoginToken &result) {
		handleTokenResult(result);
	}).fail([=](const RPCError &error) {
		showTokenError(error);
	}).send();
}

void QrWidget::handleTokenResult(const MTPauth_LoginToken &result) {
	result.match([&](const MTPDauth_loginToken &data) {
		_requestId = 0;
		showToken(data.vtoken().v);

		if (base::take(_forceRefresh)) {
			refreshCode();
		} else {
			const auto left = data.vexpires().v - base::unixtime::now();
			_refreshTimer.callOnce(std::max(left, 1) * crl::time(1000));
		}
	}, [&](const MTPDauth_loginTokenMigrateTo &data) {
		importTo(data.vdc_id().v, data.vtoken().v);
	}, [&](const MTPDauth_loginTokenSuccess &data) {
		done(data.vauthorization());
	});
}

void QrWidget::showTokenError(const RPCError &error) {
	_requestId = 0;
	if (base::take(_forceRefresh)) {
		refreshCode();
	} else {
		showError(rpl::single(error.type()));
	}
}

void QrWidget::showToken(const QByteArray &token) {
	const auto encoded = token.toBase64(QByteArray::Base64UrlEncoding);
	_qrCodes.fire_copy("tg_login/" + encoded);
}

void QrWidget::importTo(MTP::DcId dcId, const QByteArray &token) {
	Expects(_requestId != 0);

	_requestId = _api.request(MTPauth_ImportLoginToken(
		MTP_bytes(token)
	)).done([=](const MTPauth_LoginToken &result) {
		handleTokenResult(result);
	}).fail([=](const RPCError &error) {
		showTokenError(error);
	}).toDC(dcId).send();
}

void QrWidget::done(const MTPauth_Authorization &authorization) {
	authorization.match([&](const MTPDauth_authorization &data) {
		if (data.vuser().type() != mtpc_user
			|| !data.vuser().c_user().is_self()) {
			showError(rpl::single(Lang::Hard::ServerError()));
			return;
		}
		const auto phone = data.vuser().c_user().vphone().value_or_empty();
		cSetLoggedPhoneNumber(phone);
		finish(data.vuser());
	}, [&](const MTPDauth_authorizationSignUpRequired &data) {
		_requestId = 0;
		LOG(("API Error: Unexpected auth.authorizationSignUpRequired."));
		showError(rpl::single(Lang::Hard::ServerError()));
	});
}

void QrWidget::activate() {
	Step::activate();
	showChildren();
}

void QrWidget::finished() {
	Step::finished();
	_refreshTimer.cancel();
	rpcInvalidate();
	cancelled();
}

void QrWidget::cancelled() {
	_api.request(base::take(_requestId)).cancel();
}

} // namespace details
} // namespace Intro
