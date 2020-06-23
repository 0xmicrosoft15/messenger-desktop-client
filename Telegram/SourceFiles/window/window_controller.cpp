/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_controller.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_app_config.h"
#include "intro/intro_widget.h"
#include "mtproto/mtproto_config.h"
#include "ui/layers/box_content.h"
#include "ui/layers/layer_widget.h"
#include "ui/toast/toast.h"
#include "ui/emoji_config.h"
#include "chat_helpers/emoji_sets_manager.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "mainwindow.h"
#include "facades.h"
#include "app.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Window {

Controller::Controller()
: _widget(this)
, _isActiveTimer([=] { updateIsActive(); }) {
	_widget.init();
}

Controller::~Controller() {
	// We want to delete all widgets before the _sessionController.
	_widget.clearWidgets();
}

void Controller::showAccount(not_null<Main::Account*> account) {
	Window::Theme::Background()->start();

	_accountLifetime.destroy();
	_account = account;

	_account->sessionValue(
	) | rpl::start_with_next([=](Main::Session *session) {
		const auto was = base::take(_sessionController);
		_sessionController = session
			? std::make_unique<SessionController>(session, this)
			: nullptr;
		if (_sessionController) {
			_sessionController->filtersMenuChanged(
			) | rpl::start_with_next([=] {
				sideBarChanged();
			}, session->lifetime());
		}
		if (session && session->settings().dialogsFiltersEnabled()) {
			_sessionController->toggleFiltersMenu(true);
		} else {
			sideBarChanged();
		}
		_widget.updateWindowIcon();
		_widget.updateGlobalMenu();
		if (session) {
			setupMain();
		} else {
			setupIntro();
		}
	}, _accountLifetime);
}

void Controller::finishFirstShow() {
	_widget.finishFirstShow();
	checkThemeEditor();
}

void Controller::checkThemeEditor() {
	using namespace Window::Theme;

	if (const auto editing = Background()->editingTheme()) {
		showRightColumn(Box<Editor>(this, *editing));
	}
}

void Controller::setupPasscodeLock() {
	_widget.setupPasscodeLock();
}

void Controller::clearPasscodeLock() {
	if (!_account) {
		showAccount(&Core::App().activeAccount());
	} else {
		_widget.clearPasscodeLock();
	}
}

void Controller::setupIntro() {
	const auto parent = Core::App().domain().maybeLastOrSomeAuthedAccount();
	if (!parent) {
		_widget.setupIntro(Intro::EnterPoint::Start);
		return;
	}
	const auto qrLogin = parent->appConfig().get<QString>(
		"qr_login_code",
		"[not-set]");
	DEBUG_LOG(("qr_login_code in setup: %1").arg(qrLogin));
	const auto qr = (qrLogin == "primary");
	_widget.setupIntro(qr ? Intro::EnterPoint::Qr : Intro::EnterPoint::Phone);
}

void Controller::setupMain() {
	Expects(_sessionController != nullptr);

	_widget.setupMain();

	if (const auto id = Ui::Emoji::NeedToSwitchBackToId()) {
		Ui::Emoji::LoadAndSwitchTo(&_sessionController->session(), id);
	}
}

void Controller::showSettings() {
	_widget.showSettings();
}

void Controller::showToast(const QString &text) {
	Ui::Toast::Show(_widget.bodyWidget(), text);
}

void Controller::showBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options,
		anim::type animated) {
	_widget.ui_showBox(std::move(content), options, animated);
}

void Controller::showRightColumn(object_ptr<TWidget> widget) {
	_widget.showRightColumn(std::move(widget));
}

void Controller::sideBarChanged() {
	_widget.setMinimumWidth(_widget.computeMinWidth());
	_widget.updateControlsGeometry();
	_widget.fixOrder();
}

void Controller::activate() {
	_widget.activate();
}

void Controller::reActivate() {
	_widget.reActivateWindow();
}

void Controller::updateIsActiveFocus() {
	_isActiveTimer.callOnce(sessionController()
		? sessionController()->session().serverConfig().onlineFocusTimeout
		: crl::time(1000));
}

void Controller::updateIsActiveBlur() {
	_isActiveTimer.callOnce(sessionController()
		? sessionController()->session().serverConfig().offlineBlurTimeout
		: crl::time(1000));
}

void Controller::updateIsActive() {
	_widget.updateIsActive();
}

void Controller::minimize() {
	if (Global::WorkMode().value() == dbiwmTrayOnly) {
		_widget.minimizeToTray();
	} else {
		_widget.setWindowState(_widget.windowState() | Qt::WindowMinimized);
	}
}

void Controller::close() {
	if (!_widget.hideNoQuit()) {
		_widget.close();
	}
}

QPoint Controller::getPointForCallPanelCenter() const {
	Expects(_widget.windowHandle() != nullptr);

	return _widget.isActive()
		? _widget.geometry().center()
		: _widget.windowHandle()->screen()->geometry().center();
}

void Controller::tempDirDelete(int task) {
	_widget.tempDirDelete(task);
}

} // namespace Window
