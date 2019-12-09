/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_dcenter.h"

#include "mtproto/facade.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/special_config_request.h"

namespace MTP {
namespace details {
namespace {

constexpr auto kEnumerateDcTimeout = 8000; // 8 seconds timeout for help_getConfig to work (then move to other dc)
constexpr auto kSpecialRequestTimeoutMs = 6000; // 4 seconds timeout for it to work in a specially requested dc.

int IndexByType(TemporaryKeyType type) {
	switch (type) {
	case TemporaryKeyType::Regular: return 0;
	case TemporaryKeyType::MediaCluster: return 1;
	}
	Unexpected("Type value in IndexByType.");
}

int IndexByType(CreatingKeyType type) {
	switch (type) {
	case CreatingKeyType::Persistent:
	case CreatingKeyType::TemporaryRegular: return 0;
	case CreatingKeyType::TemporaryMediaCluster: return 1;
	}
	Unexpected("Creating type value in IndexByType.");
}

const char *NameOfType(CreatingKeyType type) {
	switch (type) {
	case CreatingKeyType::Persistent: return "persistent";
	case CreatingKeyType::TemporaryRegular: return "regular";
	case CreatingKeyType::TemporaryMediaCluster: return "media";
	}
	Unexpected("Type value in NameOfType.");
}

} // namespace

Dcenter::Dcenter(DcId dcId, AuthKeyPtr &&key)
: _id(dcId)
, _persistentKey(std::move(key)) {
}

DcId Dcenter::id() const {
	return _id;
}

AuthKeyPtr Dcenter::getTemporaryKey(TemporaryKeyType type) const {
	QReadLocker lock(&_mutex);
	return _temporaryKeys[IndexByType(type)];
}

AuthKeyPtr Dcenter::getPersistentKey() const {
	QReadLocker lock(&_mutex);
	return _persistentKey;
}

bool Dcenter::destroyTemporaryKey(uint64 keyId) {
	QWriteLocker lock(&_mutex);
	for (auto &key : _temporaryKeys) {
		if (key && key->keyId() == keyId) {
			key = nullptr;
			_connectionInited = false;
			return true;
		}
	}
	return false;
}

bool Dcenter::destroyConfirmedForgottenKey(uint64 keyId) {
	QWriteLocker lock(&_mutex);
	if (!_persistentKey || _persistentKey->keyId() != keyId) {
		return false;
	}
	for (auto &key : _temporaryKeys) {
		key = nullptr;
	}
	_persistentKey = nullptr;
	_connectionInited = false;
	return true;
}

bool Dcenter::connectionInited() const {
	QReadLocker lock(&_mutex);
	return _connectionInited;
}

void Dcenter::setConnectionInited(bool connectionInited) {
	QWriteLocker lock(&_mutex);
	_connectionInited = connectionInited;
}

CreatingKeyType Dcenter::acquireKeyCreation(TemporaryKeyType type) {
	QReadLocker lock(&_mutex);
	const auto index = IndexByType(type);
	auto &key = _temporaryKeys[index];
	if (key != nullptr) {
		return CreatingKeyType::None;
	}
	auto expected = false;
	const auto regular = IndexByType(TemporaryKeyType::Regular);
	if (type == TemporaryKeyType::MediaCluster && _temporaryKeys[regular]) {
		return !_creatingKeys[index].compare_exchange_strong(expected, true)
			? CreatingKeyType::None
			: CreatingKeyType::TemporaryMediaCluster;
	}
	return !_creatingKeys[regular].compare_exchange_strong(expected, true)
		? CreatingKeyType::None
		: !_persistentKey
		? CreatingKeyType::Persistent
		: CreatingKeyType::TemporaryRegular;
}

bool Dcenter::releaseKeyCreationOnDone(
		CreatingKeyType type,
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind) {
	Expects(_creatingKeys[IndexByType(type)]);
	Expects(_temporaryKeys[IndexByType(type)] == nullptr);
	Expects(temporaryKey != nullptr);

	QWriteLocker lock(&_mutex);
	if (type != CreatingKeyType::Persistent
		&& _persistentKey != persistentKeyUsedForBind) {
		return false;
	}
	if (type == CreatingKeyType::Persistent) {
		_persistentKey = persistentKeyUsedForBind;
	} else if (_persistentKey != persistentKeyUsedForBind) {
		return false;
	}
	_temporaryKeys[IndexByType(type)] = temporaryKey;
	_creatingKeys[IndexByType(type)] = false;
	_connectionInited = false;

	DEBUG_LOG(("AuthKey Info: Dcenter::releaseKeyCreationOnDone(%1, %2, %3)."
		).arg(NameOfType(type)
		).arg(temporaryKey ? temporaryKey->keyId() : 0
		).arg(persistentKeyUsedForBind
			? persistentKeyUsedForBind->keyId()
			: 0));
	return true;
}

void Dcenter::releaseKeyCreationOnFail(CreatingKeyType type) {
	Expects(_creatingKeys[IndexByType(type)]);
	Expects(_temporaryKeys[IndexByType(type)] == nullptr);

	_creatingKeys[IndexByType(type)] = false;
}

} // namespace details
} // namespace MTP
