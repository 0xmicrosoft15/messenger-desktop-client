/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "intro/introsignup.h"

#include "styles/style_intro.h"
#include "styles/style_boxes.h"
#include "ui/filedialog.h"
#include "boxes/photocropbox.h"
#include "lang.h"
#include "application.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/buttons/peer_avatar_button.h"

namespace Intro {

SignupWidget::SignupWidget(QWidget *parent, Widget::Data *data) : Step(parent, data)
, _photo(this, st::introPhotoSize, st::introPhotoIconPosition)
, _first(this, st::introName, lang(lng_signup_firstname))
, _last(this, st::introName, lang(lng_signup_lastname))
, _invertOrder(langFirstNameGoesSecond())
, _checkRequest(this) {
	connect(_checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	_photo->setClickedCallback([this] {
		auto imgExtensions = cImgExtensions();
		auto filter = qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;") + filedialogAllFilesFilter();
		_readPhotoFileQueryId = FileDialog::queryReadFile(lang(lng_choose_image), filter);
	});
	subscribe(FileDialog::QueryDone(), [this](const FileDialog::QueryUpdate &update) {
		notifyFileQueryUpdated(update);
	});

	if (_invertOrder) {
		setTabOrder(_last, _first);
	}
	setErrorCentered(true);

	setTitleText(lang(lng_signup_title));
	setDescriptionText(lang(lng_signup_desc));
	setMouseTracking(true);
}

void SignupWidget::notifyFileQueryUpdated(const FileDialog::QueryUpdate &update) {
	if (_readPhotoFileQueryId != update.queryId) {
		return;
	}
	_readPhotoFileQueryId = 0;
	if (update.remoteContent.isEmpty() && update.filePaths.isEmpty()) {
		return;
	}

	QImage img;
	if (!update.remoteContent.isEmpty()) {
		img = App::readImage(update.remoteContent);
	} else {
		img = App::readImage(update.filePaths.front());
	}

	if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
		showError(lang(lng_bad_photo));
		return;
	}
	auto box = new PhotoCropBox(img, PeerId(0));
	connect(box, SIGNAL(ready(const QImage &)), this, SLOT(onPhotoReady(const QImage &)));
	Ui::showLayer(box);
}

void SignupWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);

	auto photoRight = contentLeft() + st::introNextButton.width;
	auto photoTop = contentTop() + st::introPhotoTop;
	_photo->moveToLeft(photoRight - _photo->width(), photoTop);

	auto firstTop = contentTop() + st::introStepFieldTop;
	auto secondTop = firstTop + st::introName.height + st::introPhoneTop;
	if (_invertOrder) {
		_last->moveToLeft(contentLeft(), firstTop);
		_first->moveToLeft(contentLeft(), secondTop);
	} else {
		_first->moveToLeft(contentLeft(), firstTop);
		_last->moveToLeft(contentLeft(), secondTop);
	}
}

void SignupWidget::setInnerFocus() {
	if (_invertOrder || _last->hasFocus()) {
		_last->setFocus();
	} else {
		_first->setFocus();
	}
}

void SignupWidget::activate() {
	Step::activate();
	_first->show();
	_last->show();
	_photo->show();
	setInnerFocus();
}

void SignupWidget::cancelled() {
	MTP::cancel(base::take(_sentRequest));
}

void SignupWidget::stopCheck() {
	_checkRequest->stop();
}

void SignupWidget::onCheckRequest() {
	int32 status = MTP::state(_sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(base::take(_sentRequest));
			if (!_first->isEnabled()) {
				_first->setDisabled(false);
				_last->setDisabled(false);
				if (_invertOrder) {
					_first->setFocus();
				} else {
					_last->setFocus();
				}
			}
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void SignupWidget::onPhotoReady(const QImage &img) {
	_photoImage = img;
	_photo->setImage(_photoImage);
}

void SignupWidget::nameSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	_first->setDisabled(false);
	_last->setDisabled(false);
	const auto &d(result.c_auth_authorization());
	if (d.vuser.type() != mtpc_user || !d.vuser.c_user().is_self()) { // wtf?
		showError(lang(lng_server_error));
		return;
	}
	finish(d.vuser, _photoImage);
}

bool SignupWidget::nameSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		stopCheck();
		_first->setDisabled(false);
		_last->setDisabled(false);
		showError(lang(lng_flood_error));
		if (_invertOrder) {
			_first->setFocus();
		} else {
			_last->setFocus();
		}
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	stopCheck();
	_first->setDisabled(false);
	_last->setDisabled(false);
	const QString &err = error.type();
	if (err == qstr("PHONE_NUMBER_INVALID") || err == qstr("PHONE_CODE_EXPIRED") ||
		err == qstr("PHONE_CODE_EMPTY") || err == qstr("PHONE_CODE_INVALID") ||
		err == qstr("PHONE_NUMBER_OCCUPIED")) {
		goBack();
		return true;
	} else if (err == "FIRSTNAME_INVALID") {
		showError(lang(lng_bad_name));
		_first->setFocus();
		return true;
	} else if (err == "LASTNAME_INVALID") {
		showError(lang(lng_bad_name));
		_last->setFocus();
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	if (_invertOrder) {
		_last->setFocus();
	} else {
		_first->setFocus();
	}
	return false;
}

void SignupWidget::onInputChange() {
	showError(QString());
}

void SignupWidget::submit() {
	if (_invertOrder) {
		if ((_last->hasFocus() || _last->getLastText().trimmed().length()) && !_first->getLastText().trimmed().length()) {
			_first->setFocus();
			return;
		} else if (!_last->getLastText().trimmed().length()) {
			_last->setFocus();
			return;
		}
	} else {
		if ((_first->hasFocus() || _first->getLastText().trimmed().length()) && !_last->getLastText().trimmed().length()) {
			_last->setFocus();
			return;
		} else if (!_first->getLastText().trimmed().length()) {
			_first->setFocus();
			return;
		}
	}
	if (!_first->isEnabled()) return;

	_first->setDisabled(true);
	_last->setDisabled(true);
	setFocus();

	showError(QString());

	_firstName = _first->getLastText().trimmed();
	_lastName = _last->getLastText().trimmed();
	_sentRequest = MTP::send(MTPauth_SignUp(MTP_string(getData()->phone), MTP_string(getData()->phoneHash), MTP_string(getData()->code), MTP_string(_firstName), MTP_string(_lastName)), rpcDone(&SignupWidget::nameSubmitDone), rpcFail(&SignupWidget::nameSubmitFail));
}

QString SignupWidget::nextButtonText() const {
	return lang(lng_intro_finish);
}

} // namespace Intro
