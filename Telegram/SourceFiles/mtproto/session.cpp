/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/session.h"

#include "mtproto/connection.h"
#include "mtproto/dcenter.h"
#include "mtproto/mtproto_auth_key.h"
#include "base/unixtime.h"
#include "base/openssl_help.h"
#include "core/crash_reports.h"
#include "facades.h"

namespace MTP {
namespace internal {

ConnectionOptions::ConnectionOptions(
	const QString &systemLangCode,
	const QString &cloudLangCode,
	const QString &langPackName,
	const ProxyData &proxy,
	bool useIPv4,
	bool useIPv6,
	bool useHttp,
	bool useTcp)
: systemLangCode(systemLangCode)
, cloudLangCode(cloudLangCode)
, langPackName(langPackName)
, proxy(proxy)
, useIPv4(useIPv4)
, useIPv6(useIPv6)
, useHttp(useHttp)
, useTcp(useTcp) {
}

template <typename Callback>
void SessionData::withSession(Callback &&callback) {
	QMutexLocker lock(&_ownerMutex);
	if (const auto session = _owner) {
		InvokeQueued(session, [
			session,
			callback = std::forward<Callback>(callback)
		] {
			callback(session);
		});
	}
}

void SessionData::notifyConnectionInited(const ConnectionOptions &options) {
	// #TODO race
	const auto current = connectionOptions();
	if (current.cloudLangCode == _options.cloudLangCode
		&& current.systemLangCode == _options.systemLangCode
		&& current.langPackName == _options.langPackName
		&& current.proxy == _options.proxy) {
		QMutexLocker lock(&_ownerMutex);
		if (_owner) {
			_owner->notifyDcConnectionInited();
		}
	}
}

void SessionData::queueTryToReceive() {
	withSession([](not_null<Session*> session) {
		session->tryToReceive();
	});
}

void SessionData::queueNeedToResumeAndSend() {
	withSession([](not_null<Session*> session) {
		session->needToResumeAndSend();
	});
}

void SessionData::queueConnectionStateChange(int newState) {
	withSession([=](not_null<Session*> session) {
		session->connectionStateChange(newState);
	});
}

void SessionData::queueResetDone() {
	withSession([](not_null<Session*> session) {
		session->resetDone();
	});
}

void SessionData::queueSendAnything(crl::time msCanWait) {
	withSession([=](not_null<Session*> session) {
		session->sendAnything(msCanWait);
	});
}

bool SessionData::connectionInited() const {
	QMutexLocker lock(&_ownerMutex);
	return _owner ? _owner->connectionInited() : false;
}

AuthKeyPtr SessionData::getTemporaryKey(TemporaryKeyType type) const {
	QMutexLocker lock(&_ownerMutex);
	return _owner ? _owner->getTemporaryKey(type) : nullptr;
}

AuthKeyPtr SessionData::getPersistentKey() const {
	QMutexLocker lock(&_ownerMutex);
	return _owner ? _owner->getPersistentKey() : nullptr;
}

CreatingKeyType SessionData::acquireKeyCreation(TemporaryKeyType type) {
	QMutexLocker lock(&_ownerMutex);
	return _owner ? _owner->acquireKeyCreation(type) : CreatingKeyType::None;
}

bool SessionData::releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind) {
	QMutexLocker lock(&_ownerMutex);
	return _owner
		? _owner->releaseKeyCreationOnDone(
			temporaryKey,
			persistentKeyUsedForBind)
		: false;
}

void SessionData::releaseKeyCreationOnFail() {
	QMutexLocker lock(&_ownerMutex);
	if (_owner) {
		_owner->releaseKeyCreationOnFail();
	}
}

void SessionData::destroyTemporaryKey(uint64 keyId) {
	QMutexLocker lock(&_ownerMutex);
	if (_owner) {
		_owner->destroyTemporaryKey(keyId);
	}
}

void SessionData::detach() {
	QMutexLocker lock(&_ownerMutex);
	_owner = nullptr;
}

Session::Session(
	not_null<Instance*> instance,
	ShiftedDcId shiftedDcId,
	not_null<Dcenter*> dc)
: QObject()
, _instance(instance)
, _shiftedDcId(shiftedDcId)
, _dc(dc)
, _data(std::make_shared<SessionData>(this))
, _sender([=] { needToResumeAndSend(); }) {
	_timeouter.callEach(1000);
	refreshOptions();
	watchDcKeyChanges();
}

void Session::watchDcKeyChanges() {
	_instance->dcTemporaryKeyChanged(
	) | rpl::filter([=](DcId dcId) {
		return (dcId == _shiftedDcId) || (dcId == BareDcId(_shiftedDcId));
	}) | rpl::start_with_next([=] {
		DEBUG_LOG(("AuthKey Info: Session::authKeyCreatedForDC slot, "
			"emitting authKeyChanged(), dcWithShift %1").arg(_shiftedDcId));
		emit authKeyChanged();
	}, _lifetime);
}

void Session::start() {
	_connection = std::make_unique<Connection>(_instance);
	_connection->start(_data, _shiftedDcId);
}

bool Session::rpcErrorOccured(
		mtpRequestId requestId,
		const RPCFailHandlerPtr &onFail,
		const RPCError &error) { // return true if need to clean request data
	return _instance->rpcErrorOccured(requestId, onFail, error);
}

void Session::restart() {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't restart a killed session"));
		return;
	}
	refreshOptions();
	emit needToRestart();
}

void Session::refreshOptions() {
	const auto &proxy = Global::SelectedProxy();
	const auto proxyType =
		(Global::ProxySettings() == ProxyData::Settings::Enabled
			? proxy.type
			: ProxyData::Type::None);
	const auto useTcp = (proxyType != ProxyData::Type::Http);
	const auto useHttp = (proxyType != ProxyData::Type::Mtproto);
	const auto useIPv4 = true;
	const auto useIPv6 = Global::TryIPv6();
	_data->setConnectionOptions(ConnectionOptions(
		_instance->systemLangCode(),
		_instance->cloudLangCode(),
		_instance->langPackName(),
		(Global::ProxySettings() == ProxyData::Settings::Enabled
			? proxy
			: ProxyData()),
		useIPv4,
		useIPv6,
		useHttp,
		useTcp));
}

void Session::reInitConnection() {
	_dc->setConnectionInited(false);
	restart();
}

void Session::stop() {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't kill a killed session"));
		return;
	}
	DEBUG_LOG(("Session Info: stopping session dcWithShift %1").arg(_shiftedDcId));
	if (_connection) {
		_connection->kill();
		_instance->queueQuittingConnection(std::move(_connection));
	}
}

void Session::kill() {
	stop();
	_killed = true;
	_data->detach();
	DEBUG_LOG(("Session Info: marked session dcWithShift %1 as killed").arg(_shiftedDcId));
}

void Session::unpaused() {
	if (_needToReceive) {
		_needToReceive = false;
		InvokeQueued(this, [=] {
			tryToReceive();
		});
	}
}

void Session::sendAnything(crl::time msCanWait) {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't send anything in a killed session"));
		return;
	}
	const auto ms = crl::now();
	if (_msSendCall) {
		if (ms > _msSendCall + _msWait) {
			_msWait = 0;
		} else {
			_msWait = (_msSendCall + _msWait) - ms;
			if (_msWait > msCanWait) {
				_msWait = msCanWait;
			}
		}
	} else {
		_msWait = msCanWait;
	}
	if (_msWait) {
		DEBUG_LOG(("MTP Info: dcWithShift %1 can wait for %2ms from current %3").arg(_shiftedDcId).arg(_msWait).arg(_msSendCall));
		_msSendCall = ms;
		_sender.callOnce(_msWait);
	} else {
		DEBUG_LOG(("MTP Info: dcWithShift %1 stopped send timer, can wait for %2ms from current %3").arg(_shiftedDcId).arg(_msWait).arg(_msSendCall));
		_sender.cancel();
		_msSendCall = 0;
		needToResumeAndSend();
	}
}

void Session::needToResumeAndSend() {
	if (_killed) {
		DEBUG_LOG(("Session Info: can't resume a killed session"));
		return;
	}
	if (!_connection) {
		DEBUG_LOG(("Session Info: resuming session dcWithShift %1").arg(_shiftedDcId));
		start();
	}
	if (_ping) {
		_ping = false;
		emit needToPing();
	} else {
		emit needToSend();
	}
}

void Session::connectionStateChange(int newState) {
	_instance->onStateChange(_shiftedDcId, newState);
}

void Session::resetDone() {
	_instance->onSessionReset(_shiftedDcId);
}

void Session::cancel(mtpRequestId requestId, mtpMsgId msgId) {
	if (requestId) {
		QWriteLocker locker(_data->toSendMutex());
		_data->toSendMap().remove(requestId);
	}
	if (msgId) {
		QWriteLocker locker(_data->haveSentMutex());
		_data->haveSentMap().remove(msgId);
	}
}

void Session::ping() {
	_ping = true;
	sendAnything();
}

int32 Session::requestState(mtpRequestId requestId) const {
	int32 result = MTP::RequestSent;

	bool connected = false;
	if (_connection) {
		int32 s = _connection->state();
		if (s == ConnectedState) {
			connected = true;
		} else if (s == ConnectingState || s == DisconnectedState) {
			if (result < 0 || result == MTP::RequestSent) {
				result = MTP::RequestConnecting;
			}
		} else if (s < 0) {
			if ((result < 0 && s > result) || result == MTP::RequestSent) {
				result = s;
			}
		}
	}
	if (!connected) {
		return result;
	} else if (!requestId) {
		return MTP::RequestSent;
	}

	QWriteLocker locker(_data->toSendMutex());
	return _data->toSendMap().contains(requestId)
		? MTP::RequestSending
		: MTP::RequestSent;
}

int32 Session::getState() const {
	int32 result = -86400000;

	if (_connection) {
		int32 s = _connection->state();
		if (s == ConnectedState) {
			return s;
		} else if (s == ConnectingState || s == DisconnectedState) {
			if (result < 0) {
				return s;
			}
		} else if (s < 0) {
			if (result < 0 && s > result) {
				result = s;
			}
		}
	}
	if (result == -86400000) {
		result = DisconnectedState;
	}
	return result;
}

QString Session::transport() const {
	return _connection ? _connection->transport() : QString();
}

void Session::sendPrepared(
		const details::SerializedRequest &request,
		crl::time msCanWait) {
	DEBUG_LOG(("MTP Info: adding request to toSendMap, msCanWait %1"
		).arg(msCanWait));
	{
		QWriteLocker locker(_data->toSendMutex());
		_data->toSendMap().emplace(request->requestId, request);
		*(mtpMsgId*)(request->data() + 4) = 0;
		*(request->data() + 6) = 0;
	}

	DEBUG_LOG(("MTP Info: added, requestId %1").arg(request->requestId));
	if (msCanWait >= 0) {
		InvokeQueued(this, [=] {
			sendAnything(msCanWait);
		});
	}
}

CreatingKeyType Session::acquireKeyCreation(TemporaryKeyType type) {
	Expects(_myKeyCreation == CreatingKeyType::None);

	_myKeyCreation = _dc->acquireKeyCreation(type);
	return _myKeyCreation;
}

bool Session::releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind) {
	Expects(_myKeyCreation != CreatingKeyType::None);
	Expects(persistentKeyUsedForBind != nullptr);

	const auto wasKeyCreation = std::exchange(
		_myKeyCreation,
		CreatingKeyType::None);
	const auto result = _dc->releaseKeyCreationOnDone(
		wasKeyCreation,
		temporaryKey,
		persistentKeyUsedForBind);

	if (!result) {
		DEBUG_LOG(("AuthKey Info: Persistent key changed "
			"while binding temporary, dcWithShift %1"
			).arg(_shiftedDcId));
		return false;
	}

	DEBUG_LOG(("AuthKey Info: Session key bound, setting, dcWithShift %1"
		).arg(_shiftedDcId));

	const auto dcId = _dc->id();
	const auto instance = _instance;
	InvokeQueued(instance, [=] {
		if (wasKeyCreation == CreatingKeyType::Persistent) {
			instance->dcPersistentKeyChanged(dcId, persistentKeyUsedForBind);
		} else {
			instance->dcTemporaryKeyChanged(dcId);
		}
	});
	return true;
}

void Session::releaseKeyCreationOnFail() {
	Expects(_myKeyCreation != CreatingKeyType::None);

	const auto wasKeyCreation = std::exchange(
		_myKeyCreation,
		CreatingKeyType::None);
	_dc->releaseKeyCreationOnFail(wasKeyCreation);
}

void Session::notifyDcConnectionInited() {
	DEBUG_LOG(("MTP Info: emitting MTProtoDC::connectionWasInited(), dcWithShift %1").arg(_shiftedDcId));
	_dc->setConnectionInited();
}

void Session::destroyTemporaryKey(uint64 keyId) {
	if (!_dc->destroyTemporaryKey(keyId)) {
		return;
	}
	const auto dcId = _dc->id();
	const auto instance = _instance;
	InvokeQueued(instance, [=] {
		instance->dcTemporaryKeyChanged(dcId);
	});
}

int32 Session::getDcWithShift() const {
	return _shiftedDcId;
}

AuthKeyPtr Session::getTemporaryKey(TemporaryKeyType type) const {
	return _dc->getTemporaryKey(type);
}

AuthKeyPtr Session::getPersistentKey() const {
	return _dc->getPersistentKey();
}

bool Session::connectionInited() const {
	return _dc->connectionInited();
}

void Session::tryToReceive() {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't receive in a killed session"));
		return;
	}
	if (paused()) {
		_needToReceive = true;
		return;
	}
	while (true) {
		auto lock = QWriteLocker(_data->haveReceivedMutex());
		const auto responses = base::take(_data->haveReceivedResponses());
		const auto updates = base::take(_data->haveReceivedUpdates());
		lock.unlock();
		if (responses.empty() && updates.empty()) {
			break;
		}
		for (const auto &[requestId, response] : responses) {
			_instance->execCallback(
				requestId,
				response.constData(),
				response.constData() + response.size());
		}

		// Call globalCallback only in main session.
		if (_shiftedDcId == BareDcId(_shiftedDcId)) {
			for (const auto &update : updates) {
				_instance->globalCallback(
					update.constData(),
					update.constData() + update.size());
			}
		}
	}
}

Session::~Session() {
	if (_myKeyCreation != CreatingKeyType::None) {
		releaseKeyCreationOnFail();
	}
	Assert(_connection == nullptr);
}

} // namespace internal
} // namespace MTP
