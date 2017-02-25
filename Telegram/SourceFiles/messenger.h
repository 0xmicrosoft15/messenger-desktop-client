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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "core/observer.h"

namespace MTP {
class DcOptions;
} // namespace MTP

class AuthSession;
class MainWidget;
class FileUploader;
class Translator;

class Messenger : public QObject, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Messenger();

	Messenger(const Messenger &other) = delete;
	Messenger &operator=(const Messenger &other) = delete;

	void prepareToDestroy();
	~Messenger();

	MainWindow *mainWindow();

	static Messenger *InstancePointer();
	static Messenger &Instance() {
		auto result = InstancePointer();
		t_assert(result != nullptr);
		return *result;
	}

	MTP::DcOptions *dcOptions() {
		return _dcOptions.get();
	}
	AuthSession *authSession() {
		return _authSession.get();
	}
	void authSessionCreate(UserId userId);
	void authSessionDestroy();

	FileUploader *uploader();
	void uploadProfilePhoto(const QImage &tosend, const PeerId &peerId);
	void regPhotoUpdate(const PeerId &peer, const FullMsgId &msgId);
	bool isPhotoUpdating(const PeerId &peer);
	void cancelPhotoUpdate(const PeerId &peer);

	void selfPhotoCleared(const MTPUserProfilePhoto &result);
	void chatPhotoCleared(PeerId peer, const MTPUpdates &updates);
	void selfPhotoDone(const MTPphotos_Photo &result);
	void chatPhotoDone(PeerId peerId, const MTPUpdates &updates);
	bool peerPhotoFail(PeerId peerId, const RPCError &e);
	void peerClearPhoto(PeerId peer);

	void writeUserConfigIn(TimeMs ms);

	void killDownloadSessionsStart(int32 dc);
	void killDownloadSessionsStop(int32 dc);

	void checkLocalTime();
	void checkMapVersion();

	void handleAppActivated();
	void handleAppDeactivated();

signals:
	void peerPhotoDone(PeerId peer);
	void peerPhotoFail(PeerId peer);

public slots:
	void photoUpdated(const FullMsgId &msgId, bool silent, const MTPInputFile &file);

	void onSwitchDebugMode();
	void onSwitchWorkMode();
	void onSwitchTestMode();

	void killDownloadSessions();
	void onAppStateChanged(Qt::ApplicationState state);

	void call_handleHistoryUpdate();
	void call_handleUnreadCounterUpdate();
	void call_handleFileDialogQueue();
	void call_handleDelayedPeerUpdates();
	void call_handleObservables();

private:
	void startLocalStorage();
	void loadLanguage();

	QMap<FullMsgId, PeerId> photoUpdates;

	QMap<int32, TimeMs> killDownloadSessionTimes;
	SingleTimer killDownloadSessionsTimer;

	TimeMs _lastActionTime = 0;

	std::unique_ptr<MainWindow> _window;
	FileUploader *_uploader = nullptr;
	Translator *_translator = nullptr;

	std::unique_ptr<MTP::DcOptions> _dcOptions;
	std::unique_ptr<AuthSession> _authSession;

};
