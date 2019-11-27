/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intropwdcheck.h"

#include "intro/introwidget.h"
#include "core/file_utilities.h"
#include "core/core_cloud_password.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "intro/introsignup.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "main/main_account.h"
#include "base/openssl_help.h"
#include "styles/style_intro.h"
#include "styles/style_boxes.h"

namespace Intro {
namespace details {

PwdCheckWidget::PwdCheckWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _api(account->mtp())
, _request(getData()->pwdRequest)
, _hasRecovery(getData()->hasRecovery)
, _notEmptyPassport(getData()->pwdNotEmptyPassport)
, _hint(getData()->pwdHint)
, _pwdField(this, st::introPassword, tr::lng_signin_password())
, _pwdHint(this, st::introPasswordHint)
, _codeField(this, st::introPassword, tr::lng_signin_code())
, _toRecover(this, tr::lng_signin_recover(tr::now))
, _toPassword(this, tr::lng_signin_try_password(tr::now))
, _checkRequest(this) {
	Expects(!!_request);

	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	connect(_checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));
	_toRecover->addClickHandler([=] { onToRecover(); });
	_toPassword->addClickHandler([=] { onToPassword(); });
	connect(_pwdField, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(_codeField, SIGNAL(changed()), this, SLOT(onInputChange()));

	setTitleText(tr::lng_signin_title());
	updateDescriptionText();

	if (_hint.isEmpty()) {
		_pwdHint->hide();
	} else {
		_pwdHint->setText(
			tr::lng_signin_hint(tr::now, lt_password_hint, _hint));
	}
	_codeField->hide();
	_toPassword->hide();

	setMouseTracking(true);
}

void PwdCheckWidget::refreshLang() {
	if (_toRecover) {
		_toRecover->setText(tr::lng_signin_recover(tr::now));
	}
	if (_toPassword) {
		_toPassword->setText(
			tr::lng_signin_try_password(tr::now));
	}
	if (!_hint.isEmpty()) {
		_pwdHint->setText(
			tr::lng_signin_hint(tr::now, lt_password_hint, _hint));
	}
	updateControlsGeometry();
}

int PwdCheckWidget::errorTop() const {
	return contentTop() + st::introErrorBelowLinkTop;
}

void PwdCheckWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void PwdCheckWidget::updateControlsGeometry() {
	_pwdField->moveToLeft(contentLeft(), contentTop() + st::introPasswordTop);
	_pwdHint->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introPasswordHintTop);
	_codeField->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto linkTop = _codeField->y() + _codeField->height() + st::introLinkTop;
	_toRecover->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
	_toPassword->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
}

void PwdCheckWidget::setInnerFocus() {
	if (_pwdField->isHidden()) {
		_codeField->setFocusFast();
	} else {
		_pwdField->setFocusFast();
	}
}

void PwdCheckWidget::activate() {
	if (_pwdField->isHidden() && _codeField->isHidden()) {
		Step::activate();
		_pwdField->show();
		_pwdHint->show();
		_toRecover->show();
	}
	setInnerFocus();
}

void PwdCheckWidget::cancelled() {
	_api.request(base::take(_sentRequest)).cancel();
}

void PwdCheckWidget::stopCheck() {
	_checkRequest->stop();
}

void PwdCheckWidget::onCheckRequest() {
	auto status = MTP::state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			_api.request(base::take(_sentRequest)).cancel();
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void PwdCheckWidget::pwdSubmitDone(bool recover, const MTPauth_Authorization &result) {
	_sentRequest = 0;
	stopCheck();
	if (recover) {
		cSetPasswordRecovered(true);
	}
	auto &d = result.c_auth_authorization();
	if (d.vuser().type() != mtpc_user || !d.vuser().c_user().is_self()) { // wtf?
		showError(rpl::single(Lang::Hard::ServerError()));
		return;
	}
	finish(d.vuser());
}

void PwdCheckWidget::pwdSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		_sentRequest = 0;
		stopCheck();
		showError(tr::lng_flood_error());
		_pwdField->showError();
		return;
	}

	_sentRequest = 0;
	stopCheck();
	const auto &type = error.type();
	if (type == qstr("PASSWORD_HASH_INVALID")
		|| type == qstr("SRP_PASSWORD_CHANGED")) {
		showError(tr::lng_signin_bad_password());
		_pwdField->selectAll();
		_pwdField->showError();
	} else if (type == qstr("PASSWORD_EMPTY")
		|| type == qstr("AUTH_KEY_UNREGISTERED")) {
		goBack();
	} else if (type == qstr("SRP_ID_INVALID")) {
		handleSrpIdInvalid();
	} else {
		if (Logs::DebugEnabled()) { // internal server error
			showError(rpl::single(type + ": " + error.description()));
		} else {
			showError(rpl::single(Lang::Hard::ServerError()));
		}
		_pwdField->setFocus();
	}
}

void PwdCheckWidget::handleSrpIdInvalid() {
	const auto now = crl::now();
	if (_lastSrpIdInvalidTime > 0
		&& now - _lastSrpIdInvalidTime < Core::kHandleSrpIdInvalidTimeout) {
		_request.id = 0;
		showError(rpl::single(Lang::Hard::ServerError()));
	} else {
		_lastSrpIdInvalidTime = now;
		requestPasswordData();
	}
}

void PwdCheckWidget::checkPasswordHash() {
	if (_request.id) {
		passwordChecked();
	} else {
		requestPasswordData();
	}
}

void PwdCheckWidget::requestPasswordData() {
	_api.request(base::take(_sentRequest)).cancel();
	_sentRequest = _api.request(
		MTPaccount_GetPassword()
	).done([=](const MTPaccount_Password &result) {
		_sentRequest = 0;
		result.match([&](const MTPDaccount_password &data) {
			_request = Core::ParseCloudPasswordCheckRequest(data);
			passwordChecked();
		});
	}).send();
}

void PwdCheckWidget::passwordChecked() {
	if (!_request || !_request.id) {
		return serverError();
	}
	const auto check = Core::ComputeCloudPasswordCheck(
		_request,
		_passwordHash);
	if (!check) {
		return serverError();
	}
	_request.id = 0;
	_sentRequest = _api.request(
		MTPauth_CheckPassword(check.result)
	).done([=](const MTPauth_Authorization &result) {
		pwdSubmitDone(false, result);
	}).handleFloodErrors().fail([=](const RPCError &error) {
		pwdSubmitFail(error);
	}).send();
}

void PwdCheckWidget::serverError() {
	showError(rpl::single(Lang::Hard::ServerError()));
}

void PwdCheckWidget::codeSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		showError(tr::lng_flood_error());
		_codeField->showError();
		return;
	}

	_sentRequest = 0;
	stopCheck();
	const auto &type = error.type();
	if (type == qstr("PASSWORD_EMPTY")
		|| type == qstr("AUTH_KEY_UNREGISTERED")) {
		goBack();
	} else if (type == qstr("PASSWORD_RECOVERY_NA")) {
		recoverStartFail(error);
	} else if (type == qstr("PASSWORD_RECOVERY_EXPIRED")) {
		_emailPattern = QString();
		onToPassword();
	} else if (type == qstr("CODE_INVALID")) {
		showError(tr::lng_signin_wrong_code());
		_codeField->selectAll();
		_codeField->showError();
	} else {
		if (Logs::DebugEnabled()) { // internal server error
			showError(rpl::single(type + ": " + error.description()));
		} else {
			showError(rpl::single(Lang::Hard::ServerError()));
		}
		_codeField->setFocus();
	}
}

void PwdCheckWidget::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_emailPattern = qs(result.c_auth_passwordRecovery().vemail_pattern());
	updateDescriptionText();
}

void PwdCheckWidget::recoverStartFail(const RPCError &error) {
	stopCheck();
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_pwdField->setFocus();
	updateDescriptionText();
	update();
	hideError();
}

void PwdCheckWidget::onToRecover() {
	if (_hasRecovery) {
		if (_sentRequest) {
			_api.request(base::take(_sentRequest)).cancel();
		}
		hideError();
		_toRecover->hide();
		_toPassword->show();
		_pwdField->hide();
		_pwdHint->hide();
		_pwdField->setText(QString());
		_codeField->show();
		_codeField->setFocus();
		updateDescriptionText();
		if (_emailPattern.isEmpty()) {
			_api.request(
				MTPauth_RequestPasswordRecovery()
			).done([=](const MTPauth_PasswordRecovery &result) {
				recoverStarted(result);
			}).fail([=](const RPCError &error) {
				recoverStartFail(error);
			}).send();
		}
	} else {
		Ui::show(Box<InformBox>(tr::lng_signin_no_email_forgot(tr::now), [this] { showReset(); }));
	}
}

void PwdCheckWidget::onToPassword() {
	Ui::show(Box<InformBox>(tr::lng_signin_cant_email_forgot(tr::now), [this] { showReset(); }));
}

void PwdCheckWidget::showReset() {
	if (_sentRequest) {
		_api.request(base::take(_sentRequest)).cancel();
	}
	_toRecover->show();
	_toPassword->hide();
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_codeField->setText(QString());
	_pwdField->setFocus();
	showResetButton();
	updateDescriptionText();
	update();
}

void PwdCheckWidget::updateDescriptionText() {
	auto pwdHidden = _pwdField->isHidden();
	auto emailPattern = _emailPattern;
	setDescriptionText(pwdHidden
		? tr::lng_signin_recover_desc(lt_email, rpl::single(emailPattern))
		: tr::lng_signin_desc());
}

void PwdCheckWidget::onInputChange() {
	hideError();
}

void PwdCheckWidget::submit() {
	if (_sentRequest) return;
	if (_pwdField->isHidden()) {
		auto code = _codeField->getLastText().trimmed();
		if (code.isEmpty()) {
			_codeField->showError();
			return;
		}
		const auto send = crl::guard(this, [=] {
			_sentRequest = _api.request(
				MTPauth_RecoverPassword(MTP_string(code))
			).done([=](const MTPauth_Authorization &result) {
				pwdSubmitDone(true, result);
			}).handleFloodErrors().fail([=](const RPCError &error) {
				codeSubmitFail(error);
			}).send();
		});

		if (_notEmptyPassport) {
			const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
			const auto confirmed = [=] {
				send();
				if (*box) {
					(*box)->closeBox();
				}
			};
			*box = Ui::show(Box<ConfirmBox>(
				tr::lng_cloud_password_passport_losing(tr::now),
				tr::lng_continue(tr::now),
				confirmed));
		} else {
			send();
		}
	} else {
		hideError();

		const auto password = _pwdField->getLastText().toUtf8();
		_passwordHash = Core::ComputeCloudPasswordHash(
			_request.algo,
			bytes::make_span(password));
		checkPasswordHash();
	}
}

rpl::producer<QString> PwdCheckWidget::nextButtonText() const {
	return tr::lng_intro_submit();
}

} // namespace details
} // namespace Intro
