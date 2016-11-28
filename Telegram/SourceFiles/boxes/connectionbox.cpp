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
#include "boxes/connectionbox.h"

#include "lang.h"
#include "localstorage.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "history/history_location_manager.h"
#include "styles/style_boxes.h"

ConnectionBox::ConnectionBox() : AbstractBox(st::boxWidth, lang(lng_connection_header))
, _hostInput(this, st::connectionHostInputField, lang(lng_connection_host_ph), Global::ConnectionProxy().host)
, _portInput(this, st::connectionPortInputField, lang(lng_connection_port_ph), QString::number(Global::ConnectionProxy().port))
, _userInput(this, st::connectionUserInputField, lang(lng_connection_user_ph), Global::ConnectionProxy().user)
, _passwordInput(this, st::connectionPasswordInputField, lang(lng_connection_password_ph), Global::ConnectionProxy().password)
, _autoRadio(this, qsl("conn_type"), dbictAuto, lang(lng_connection_auto_rb), (Global::ConnectionType() == dbictAuto))
, _httpProxyRadio(this, qsl("conn_type"), dbictHttpProxy, lang(lng_connection_http_proxy_rb), (Global::ConnectionType() == dbictHttpProxy))
, _tcpProxyRadio(this, qsl("conn_type"), dbictTcpProxy, lang(lng_connection_tcp_proxy_rb), (Global::ConnectionType() == dbictTcpProxy))
, _tryIPv6(this, lang(lng_connection_try_ipv6), Global::TryIPv6(), st::defaultBoxCheckbox)
, _save(this, lang(lng_connection_save), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {

	connect(_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(_autoRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_httpProxyRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_tcpProxyRadio, SIGNAL(changed()), this, SLOT(onChange()));

	connect(_hostInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_portInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_userInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_passwordInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	updateControlsVisibility();
}

void ConnectionBox::updateControlsVisibility() {
	int32 h = titleHeight() + st::boxOptionListPadding.top() + _autoRadio->height() + st::boxOptionListPadding.top() + _httpProxyRadio->height() + st::boxOptionListPadding.top() + _tcpProxyRadio->height() + st::boxOptionListPadding.top() + st::connectionIPv6Skip + _tryIPv6->height() + st::boxOptionListPadding.bottom() + st::boxPadding.bottom() + st::boxButtonPadding.top() + _save->height() + st::boxButtonPadding.bottom();
	if (_httpProxyRadio->checked() || _tcpProxyRadio->checked()) {
		h += 2 * st::boxOptionListPadding.top() + 2 * _hostInput->height();
		_hostInput->show();
		_portInput->show();
		_userInput->show();
		_passwordInput->show();
	} else {
		_hostInput->hide();
		_portInput->hide();
		_userInput->hide();
		_passwordInput->hide();
	}

	setMaxHeight(h);
	resizeEvent(0);
}

void ConnectionBox::doSetInnerFocus() {
	if (!_hostInput->isHidden()) {
		_hostInput->setFocus();
	}
}

void ConnectionBox::resizeEvent(QResizeEvent *e) {
	_autoRadio->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), titleHeight() + st::boxOptionListPadding.top());
	_httpProxyRadio->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _autoRadio->y() + _autoRadio->height() + st::boxOptionListPadding.top());

	int32 inputy = 0;
	if (_httpProxyRadio->checked()) {
		inputy = _httpProxyRadio->y() + _httpProxyRadio->height() + st::boxOptionListPadding.top();
		_tcpProxyRadio->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), inputy + st::boxOptionListPadding.top() + 2 * _hostInput->height() + st::boxOptionListPadding.top());
	} else {
		_tcpProxyRadio->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _httpProxyRadio->y() + _httpProxyRadio->height() + st::boxOptionListPadding.top());
		if (_tcpProxyRadio->checked()) {
			inputy = _tcpProxyRadio->y() + _tcpProxyRadio->height() + st::boxOptionListPadding.top();
		}
	}

	if (inputy) {
		_hostInput->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultRadiobutton.textPosition.x() - st::defaultInputField.textMargins.left(), inputy);
		_portInput->moveToRight(st::boxPadding.right(), inputy);
		_userInput->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultRadiobutton.textPosition.x() - st::defaultInputField.textMargins.left(), _hostInput->y() + _hostInput->height() + st::boxOptionListPadding.top());
		_passwordInput->moveToRight(st::boxPadding.right(), _userInput->y());
	}

	int32 tryipv6y = (_tcpProxyRadio->checked() ? (_userInput->y() + _userInput->height()) : (_tcpProxyRadio->y() + _tcpProxyRadio->height())) + st::boxOptionListPadding.top() + st::connectionIPv6Skip;
	_tryIPv6->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), tryipv6y);

	_save->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _save->width() + st::boxButtonPadding.left(), _save->y());

	AbstractBox::resizeEvent(e);
}

void ConnectionBox::onChange() {
	updateControlsVisibility();
	if (_httpProxyRadio->checked() || _tcpProxyRadio->checked()) {
		_hostInput->setFocus();
		if (_httpProxyRadio->checked() && !_portInput->getLastText().toInt()) {
			_portInput->setText(qsl("80"));
			_portInput->updatePlaceholder();
		}
	}
	update();
}

void ConnectionBox::onSubmit() {
	if (_hostInput->hasFocus()) {
		if (!_hostInput->getLastText().trimmed().isEmpty()) {
			_portInput->setFocus();
		} else {
			_hostInput->showError();
		}
	} else if (_portInput->hasFocus()) {
		if (_portInput->getLastText().trimmed().toInt() > 0) {
			_userInput->setFocus();
		} else {
			_portInput->showError();
		}
	} else if (_userInput->hasFocus()) {
		_passwordInput->setFocus();
	} else if (_passwordInput->hasFocus()) {
		if (_hostInput->getLastText().trimmed().isEmpty()) {
			_hostInput->setFocus();
			_hostInput->showError();
		} else if (_portInput->getLastText().trimmed().toInt() <= 0) {
			_portInput->setFocus();
			_portInput->showError();
		} else {
			onSave();
		}
	}
}

void ConnectionBox::onSave() {
	if (_httpProxyRadio->checked() || _tcpProxyRadio->checked()) {
		ProxyData p;
		p.host = _hostInput->getLastText().trimmed();
		p.user = _userInput->getLastText().trimmed();
		p.password = _passwordInput->getLastText().trimmed();
		p.port = _portInput->getLastText().toInt();
		if (p.host.isEmpty()) {
			_hostInput->setFocus();
			return;
		} else if (!p.port) {
			_portInput->setFocus();
			return;
		}
		if (_httpProxyRadio->checked()) {
			Global::SetConnectionType(dbictHttpProxy);
		} else {
			Global::SetConnectionType(dbictTcpProxy);
		}
		Global::SetConnectionProxy(p);
	} else {
		Global::SetConnectionType(dbictAuto);
		Global::SetConnectionProxy(ProxyData());
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
		QNetworkProxyFactory::setUseSystemConfiguration(false);
		QNetworkProxyFactory::setUseSystemConfiguration(true);
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	}
	if (cPlatform() == dbipWindows && Global::TryIPv6() != _tryIPv6->checked()) {
		Global::SetTryIPv6(_tryIPv6->checked());
		Local::writeSettings();
		Global::RefConnectionTypeChanged().notify();

		App::restart();
	} else {
		Global::SetTryIPv6(_tryIPv6->checked());
		Local::writeSettings();
		Global::RefConnectionTypeChanged().notify();

		MTP::restart();
		reinitLocationManager();
		reinitWebLoadManager();
		onClose();
	}
}

AutoDownloadBox::AutoDownloadBox() : AbstractBox(st::boxWidth)
, _photoPrivate(this, lang(lng_media_auto_private_chats), !(cAutoDownloadPhoto() & dbiadNoPrivate), st::defaultBoxCheckbox)
, _photoGroups(this,  lang(lng_media_auto_groups), !(cAutoDownloadPhoto() & dbiadNoGroups), st::defaultBoxCheckbox)
, _audioPrivate(this, lang(lng_media_auto_private_chats), !(cAutoDownloadAudio() & dbiadNoPrivate), st::defaultBoxCheckbox)
, _audioGroups(this, lang(lng_media_auto_groups), !(cAutoDownloadAudio() & dbiadNoGroups), st::defaultBoxCheckbox)
, _gifPrivate(this, lang(lng_media_auto_private_chats), !(cAutoDownloadGif() & dbiadNoPrivate), st::defaultBoxCheckbox)
, _gifGroups(this, lang(lng_media_auto_groups), !(cAutoDownloadGif() & dbiadNoGroups), st::defaultBoxCheckbox)
, _gifPlay(this, lang(lng_media_auto_play), cAutoPlayGif(), st::defaultBoxCheckbox)
, _sectionHeight(titleHeight() + 2 * (st::defaultBoxCheckbox.height + st::setLittleSkip))
, _save(this, lang(lng_connection_save), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {

	setMaxHeight(3 * _sectionHeight + st::setLittleSkip + _gifPlay->height() + st::setLittleSkip + st::boxButtonPadding.top() + _save->height() + st::boxButtonPadding.bottom());

	connect(_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
}

void AutoDownloadBox::paintEvent(QPaintEvent *e) {
	AbstractBox::paintEvent(e);

	Painter p(this);

	p.setPen(st::boxTextFg);
	p.setFont(st::semiboldFont);
	p.drawTextLeft(st::boxTitlePosition.x(), st::boxTitlePosition.y(), width(), lang(lng_media_auto_photo));
	p.drawTextLeft(st::boxTitlePosition.x(), _sectionHeight + st::boxTitlePosition.y(), width(), lang(lng_media_auto_audio));
	p.drawTextLeft(st::boxTitlePosition.x(), 2 * _sectionHeight + st::boxTitlePosition.y(), width(), lang(lng_media_auto_gif));
}

void AutoDownloadBox::resizeEvent(QResizeEvent *e) {
	_photoPrivate->moveToLeft(st::boxTitlePosition.x(), titleHeight() + st::setLittleSkip);
	_photoGroups->moveToLeft(st::boxTitlePosition.x(), _photoPrivate->y() + _photoPrivate->height() + st::setLittleSkip);

	_audioPrivate->moveToLeft(st::boxTitlePosition.x(), _sectionHeight + titleHeight() + st::setLittleSkip);
	_audioGroups->moveToLeft(st::boxTitlePosition.x(), _audioPrivate->y() + _audioPrivate->height() + st::setLittleSkip);

	_gifPrivate->moveToLeft(st::boxTitlePosition.x(), 2 * _sectionHeight + titleHeight() + st::setLittleSkip);
	_gifGroups->moveToLeft(st::boxTitlePosition.x(), _gifPrivate->y() + _gifPrivate->height() + st::setLittleSkip);
	_gifPlay->moveToLeft(st::boxTitlePosition.x(), _gifGroups->y() + _gifGroups->height() + st::setLittleSkip);

	_save->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _save->width() + st::boxButtonPadding.left(), _save->y());

	AbstractBox::resizeEvent(e);
}

void AutoDownloadBox::onSave() {
	bool changed = false;
	int32 autoDownloadPhoto = (_photoPrivate->checked() ? 0 : dbiadNoPrivate) | (_photoGroups->checked() ? 0 : dbiadNoGroups);
	if (cAutoDownloadPhoto() != autoDownloadPhoto) {
		bool enabledPrivate = ((cAutoDownloadPhoto() & dbiadNoPrivate) && !(autoDownloadPhoto & dbiadNoPrivate));
		bool enabledGroups = ((cAutoDownloadPhoto() & dbiadNoGroups) && !(autoDownloadPhoto & dbiadNoGroups));
		cSetAutoDownloadPhoto(autoDownloadPhoto);
		if (enabledPrivate || enabledGroups) {
			const PhotosData &data(App::photosData());
			for (PhotosData::const_iterator i = data.cbegin(), e = data.cend(); i != e; ++i) {
				i.value()->automaticLoadSettingsChanged();
			}
		}
		changed = true;
	}
	int32 autoDownloadAudio = (_audioPrivate->checked() ? 0 : dbiadNoPrivate) | (_audioGroups->checked() ? 0 : dbiadNoGroups);
	if (cAutoDownloadAudio() != autoDownloadAudio) {
		bool enabledPrivate = ((cAutoDownloadAudio() & dbiadNoPrivate) && !(autoDownloadAudio & dbiadNoPrivate));
		bool enabledGroups = ((cAutoDownloadAudio() & dbiadNoGroups) && !(autoDownloadAudio & dbiadNoGroups));
		cSetAutoDownloadAudio(autoDownloadAudio);
		if (enabledPrivate || enabledGroups) {
			const DocumentsData &data(App::documentsData());
			for (DocumentsData::const_iterator i = data.cbegin(), e = data.cend(); i != e; ++i) {
				if (i.value()->voice()) {
					i.value()->automaticLoadSettingsChanged();
				}
			}
		}
		changed = true;
	}
	int32 autoDownloadGif = (_gifPrivate->checked() ? 0 : dbiadNoPrivate) | (_gifGroups->checked() ? 0 : dbiadNoGroups);
	if (cAutoDownloadGif() != autoDownloadGif) {
		bool enabledPrivate = ((cAutoDownloadGif() & dbiadNoPrivate) && !(autoDownloadGif & dbiadNoPrivate));
		bool enabledGroups = ((cAutoDownloadGif() & dbiadNoGroups) && !(autoDownloadGif & dbiadNoGroups));
		cSetAutoDownloadGif(autoDownloadGif);
		if (enabledPrivate || enabledGroups) {
			const DocumentsData &data(App::documentsData());
			for (DocumentsData::const_iterator i = data.cbegin(), e = data.cend(); i != e; ++i) {
				if (i.value()->isAnimation()) {
					i.value()->automaticLoadSettingsChanged();
				}
			}
		}
		changed = true;
	}
	if (cAutoPlayGif() != _gifPlay->checked()) {
		cSetAutoPlayGif(_gifPlay->checked());
		if (!cAutoPlayGif()) {
			App::stopGifItems();
		}
		changed = true;
	}
	if (changed) Local::writeUserSettings();
	onClose();
}
