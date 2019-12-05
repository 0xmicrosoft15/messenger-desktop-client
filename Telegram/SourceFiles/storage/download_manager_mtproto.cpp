/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/download_manager_mtproto.h"

#include "mtproto/facade.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_rpc_sender.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "base/openssl_help.h"

namespace Storage {
namespace {

constexpr auto kKillSessionTimeout = 15 * crl::time(1000);
constexpr auto kMaxWaitedInSession = 2 * 1024 * 1024;
constexpr auto kStartSessionsCount = 1;
constexpr auto kMaxSessionsCount = 8;
constexpr auto kMaxTrackedSessionRemoves = 64;
constexpr auto kRetryAddSessionTimeout = 8 * crl::time(1000);
constexpr auto kRetryAddSessionSuccesses = 3;
constexpr auto kMaxTrackedSuccesses = kRetryAddSessionSuccesses
	* kMaxTrackedSessionRemoves;
constexpr auto kRemoveSessionAfterTimeouts = 2;
constexpr auto kResetDownloadPrioritiesTimeout = crl::time(200);

// Each session remove by timeouts we wait for time
// kRetryAddSessionTimeout * max(removesCount, kMaxTrackedSessionRemoves)
// and for successes in all remaining sessions
// kRetryAddSessionSuccesses * max(removesCount, kMaxTrackedSessionRemoves)

} // namespace

void DownloadManagerMtproto::Queue::enqueue(not_null<Task*> task) {
	const auto i = ranges::find(_tasks, task);
	if (i != end(_tasks)) {
		return;
	}
	_tasks.push_back(task);
	_previousGeneration.erase(
		ranges::remove(_previousGeneration, task),
		end(_previousGeneration));
}

void DownloadManagerMtproto::Queue::remove(not_null<Task*> task) {
	_tasks.erase(ranges::remove(_tasks, task), end(_tasks));
	_previousGeneration.erase(
		ranges::remove(_previousGeneration, task),
		end(_previousGeneration));
}

void DownloadManagerMtproto::Queue::resetGeneration() {
	if (!_previousGeneration.empty()) {
		_tasks.reserve(_tasks.size() + _previousGeneration.size());
		std::copy(
			begin(_previousGeneration),
			end(_previousGeneration),
			std::back_inserter(_tasks));
		_previousGeneration.clear();
	}
	std::swap(_tasks, _previousGeneration);
}

bool DownloadManagerMtproto::Queue::empty() const {
	return _tasks.empty() && _previousGeneration.empty();
}

auto DownloadManagerMtproto::Queue::nextTask() const -> Task* {
	auto &&all = ranges::view::concat(_tasks, _previousGeneration);
	const auto i = ranges::find(all, true, &Task::readyToRequest);
	return (i != all.end()) ? i->get() : nullptr;
}

DownloadManagerMtproto::DcSessionBalanceData::DcSessionBalanceData()
: maxWaitedAmount(kDownloadPartSize) {
}

DownloadManagerMtproto::DcBalanceData::DcBalanceData()
: sessions(kStartSessionsCount) {
}

DownloadManagerMtproto::DownloadManagerMtproto(not_null<ApiWrap*> api)
: _api(api)
, _resetGenerationTimer([=] { resetGeneration(); })
, _killSessionsTimer([=] { killSessions(); }) {
	_api->instance()->restartsByTimeout(
	) | rpl::filter([](MTP::ShiftedDcId shiftedDcId) {
		return MTP::isDownloadDcId(shiftedDcId);
	}) | rpl::start_with_next([=](MTP::ShiftedDcId shiftedDcId) {
		sessionTimedOut(
			MTP::BareDcId(shiftedDcId),
			MTP::GetDcIdShift(shiftedDcId));
	}, _lifetime);
}

DownloadManagerMtproto::~DownloadManagerMtproto() {
	killSessions();
}

void DownloadManagerMtproto::enqueue(not_null<Task*> task) {
	const auto dcId = task->dcId();
	auto &queue = _queues[dcId];
	queue.enqueue(task);
	if (!_resetGenerationTimer.isActive()) {
		_resetGenerationTimer.callOnce(kResetDownloadPrioritiesTimeout);
	}
	checkSendNext(dcId, queue);
}

void DownloadManagerMtproto::remove(not_null<Task*> task) {
	const auto dcId = task->dcId();
	auto &queue = _queues[dcId];
	queue.remove(task);
}

void DownloadManagerMtproto::resetGeneration() {
	_resetGenerationTimer.cancel();
	for (auto &[dcId, queue] : _queues) {
		queue.resetGeneration();
	}
}

void DownloadManagerMtproto::checkSendNext() {
	for (auto &[dcId, queue] : _queues) {
		if (queue.empty()) {
			continue;
		}
		checkSendNext(dcId, queue);
	}
}

void DownloadManagerMtproto::checkSendNext(MTP::DcId dcId, Queue &queue) {
	while (trySendNextPart(dcId, queue)) {
	}
}

bool DownloadManagerMtproto::trySendNextPart(MTP::DcId dcId, Queue &queue) {
	const auto bestIndex = [&] {
		const auto &sessions = _balanceData[dcId].sessions;
		const auto proj = [](const DcSessionBalanceData &data) {
			return (data.requested < data.maxWaitedAmount)
				? data.requested
				: kMaxWaitedInSession;
		};
		const auto j = ranges::min_element(sessions, ranges::less(), proj);
		return (j->requested + kDownloadPartSize <= j->maxWaitedAmount)
			? (j - begin(sessions))
			: -1;
	}();
	if (bestIndex < 0) {
		return false;
	}
	if (const auto task = queue.nextTask()) {
		task->loadPart(bestIndex);
		return true;
	}
	return false;
}

void DownloadManagerMtproto::changeRequestedAmount(
		MTP::DcId dcId,
		int index,
		int delta) {
	const auto i = _balanceData.find(dcId);
	Assert(i != _balanceData.end());
	Assert(index < i->second.sessions.size());
	i->second.sessions[index].requested += delta;
	const auto findNonEmptySession = [](const DcBalanceData &data) {
		using namespace rpl::mappers;
		return ranges::find_if(
			data.sessions,
			_1 > 0,
			&DcSessionBalanceData::requested);
	};
	if (delta > 0) {
		killSessionsCancel(dcId);
	} else if (findNonEmptySession(i->second) == end(i->second.sessions)) {
		killSessionsSchedule(dcId);
	}
}

void DownloadManagerMtproto::requestSucceeded(MTP::DcId dcId, int index) {
	using namespace rpl::mappers;

	DEBUG_LOG(("Download (%1,%2) request done.").arg(dcId).arg(index));
	const auto i = _balanceData.find(dcId);
	Assert(i != end(_balanceData));
	auto &dc = i->second;
	Assert(index < dc.sessions.size());
	auto &data = dc.sessions[index];
	data.successes = std::min(data.successes + 1, kMaxTrackedSuccesses);
	data.maxWaitedAmount = std::min(
		data.maxWaitedAmount + kDownloadPartSize,
		kMaxWaitedInSession);
	const auto notEnough = ranges::find_if(
		dc.sessions,
		_1 < (dc.sessionRemoveTimes + 1) * kRetryAddSessionSuccesses,
		&DcSessionBalanceData::successes);
	if (notEnough != end(dc.sessions)) {
		return;
	}
	for (auto &session : dc.sessions) {
		session.successes = 0;
	}
	if (dc.timeouts > 0) {
		--dc.timeouts;
		return;
	} else if (dc.sessions.size() == kMaxSessionsCount) {
		return;
	}
	const auto now = crl::now();
	const auto delay = (dc.sessionRemoveTimes + 1) * kRetryAddSessionTimeout;
	if (dc.lastSessionRemove && now < dc.lastSessionRemove + delay) {
		return;
	}
	DEBUG_LOG(("Download (%1,%2) added session."
		).arg(dcId
		).arg(dc.sessions.size()));
	dc.sessions.emplace_back();
	checkSendNext(dcId, _queues[dcId]);
}

void DownloadManagerMtproto::sessionTimedOut(MTP::DcId dcId, int index) {
	const auto i = _balanceData.find(dcId);
	if (i == end(_balanceData)) {
		return;
	}
	auto &dc = i->second;
	if (index >= dc.sessions.size()) {
		return;
	}
	DEBUG_LOG(("Download (%1,%2) session timed-out.").arg(dcId).arg(index));
	for (auto &session : dc.sessions) {
		session.successes = 0;
	}
	if (dc.sessions.size() == kStartSessionsCount
		|| ++dc.timeouts < kRemoveSessionAfterTimeouts) {
		return;
	}
	dc.timeouts = 0;
	removeSession(dcId);
}

void DownloadManagerMtproto::removeSession(MTP::DcId dcId) {
	auto &dc = _balanceData[dcId];
	Assert(dc.sessions.size() > kStartSessionsCount);
	const auto index = int(dc.sessions.size() - 1);
	DEBUG_LOG(("Download (%1,%2) removing session.").arg(dcId).arg(index));
	auto &queue = _queues[dcId];
	if (dc.sessionRemoveIndex == index) {
		dc.sessionRemoveTimes = std::min(
			dc.sessionRemoveTimes + 1,
			kMaxTrackedSessionRemoves);
	} else {
		dc.sessionRemoveIndex = index;
		dc.sessionRemoveTimes = 1;
	}
	dc.lastSessionRemove = crl::now();
//	dc.sessions.pop_back();
}

void DownloadManagerMtproto::killSessionsSchedule(MTP::DcId dcId) {
	if (!_killSessionsWhen.contains(dcId)) {
		_killSessionsWhen.emplace(dcId, crl::now() + kKillSessionTimeout);
	}
	if (!_killSessionsTimer.isActive()) {
		_killSessionsTimer.callOnce(kKillSessionTimeout + 5);
	}
}

void DownloadManagerMtproto::killSessionsCancel(MTP::DcId dcId) {
	_killSessionsWhen.erase(dcId);
	if (_killSessionsWhen.empty()) {
		_killSessionsTimer.cancel();
	}
}

void DownloadManagerMtproto::killSessions() {
	const auto now = crl::now();
	auto left = kKillSessionTimeout;
	for (auto i = begin(_killSessionsWhen); i != end(_killSessionsWhen); ) {
		if (i->second <= now) {
			killSessions(i->first);
			i = _killSessionsWhen.erase(i);
		} else {
			if (i->second - now < left) {
				left = i->second - now;
			}
			++i;
		}
	}
	if (!_killSessionsWhen.empty()) {
		_killSessionsTimer.callOnce(left);
	}
}

void DownloadManagerMtproto::killSessions(MTP::DcId dcId) {
	const auto i = _balanceData.find(dcId);
	if (i != end(_balanceData)) {
		auto &dc = i->second;
		auto sessions = base::take(dc.sessions);
		dc = DcBalanceData();
		for (auto j = 0; j != int(sessions.size()); ++j) {
			Assert(sessions[j].requested == 0);
			sessions[j] = DcSessionBalanceData();
			MTP::stopSession(MTP::downloadDcId(dcId, j));
		}
		dc.sessions = base::take(sessions);
	}
}

DownloadMtprotoTask::DownloadMtprotoTask(
	not_null<DownloadManagerMtproto*> owner,
	const StorageFileLocation &location,
	Data::FileOrigin origin)
: _owner(owner)
, _dcId(location.dcId())
, _location({ location })
, _origin(origin) {
}

DownloadMtprotoTask::DownloadMtprotoTask(
	not_null<DownloadManagerMtproto*> owner,
	MTP::DcId dcId,
	const Location &location)
: _owner(owner)
, _dcId(dcId)
, _location(location) {
}

DownloadMtprotoTask::~DownloadMtprotoTask() {
	cancelAllRequests();
	_owner->remove(this);
}

MTP::DcId DownloadMtprotoTask::dcId() const {
	return _dcId;
}

Data::FileOrigin DownloadMtprotoTask::fileOrigin() const {
	return _origin;
}

uint64 DownloadMtprotoTask::objectId() const {
	if (const auto v = base::get_if<StorageFileLocation>(&_location.data)) {
		return v->objectId();
	}
	return 0;
}

const DownloadMtprotoTask::Location &DownloadMtprotoTask::location() const {
	return _location;
}

void DownloadMtprotoTask::refreshFileReferenceFrom(
		const Data::UpdatedFileReferences &updates,
		int requestId,
		const QByteArray &current) {
	if (const auto v = base::get_if<StorageFileLocation>(&_location.data)) {
		v->refreshFileReference(updates);
		if (v->fileReference() == current) {
			cancelOnFail();
			return;
		}
	} else {
		cancelOnFail();
		return;
	}
	if (_sentRequests.contains(requestId)) {
		makeRequest(finishSentRequest(
			requestId,
			FinishRequestReason::Redirect));
	}
}

void DownloadMtprotoTask::loadPart(int dcIndex) {
	makeRequest({ takeNextRequestOffset(), dcIndex });
}

mtpRequestId DownloadMtprotoTask::sendRequest(const RequestData &requestData) {
	const auto offset = requestData.offset;
	const auto limit = Storage::kDownloadPartSize;
	const auto shiftedDcId = MTP::downloadDcId(
		_cdnDcId ? _cdnDcId : dcId(),
		requestData.dcIndex);
	if (_cdnDcId) {
		return api().request(MTPupload_GetCdnFile(
			MTP_bytes(_cdnToken),
			MTP_int(offset),
			MTP_int(limit)
		)).done([=](const MTPupload_CdnFile &result, mtpRequestId id) {
			cdnPartLoaded(result, id);
		}).fail([=](const RPCError &error, mtpRequestId id) {
			cdnPartFailed(error, id);
		}).toDC(shiftedDcId).send();
	}
	return _location.data.match([&](const WebFileLocation &location) {
		return api().request(MTPupload_GetWebFile(
			MTP_inputWebFileLocation(
				MTP_bytes(location.url()),
				MTP_long(location.accessHash())),
			MTP_int(offset),
			MTP_int(limit)
		)).done([=](const MTPupload_WebFile &result, mtpRequestId id) {
			webPartLoaded(result, id);
		}).fail([=](const RPCError &error, mtpRequestId id) {
			partFailed(error, id);
		}).toDC(shiftedDcId).send();
	}, [&](const GeoPointLocation &location) {
		return api().request(MTPupload_GetWebFile(
			MTP_inputWebFileGeoPointLocation(
				MTP_inputGeoPoint(
					MTP_double(location.lat),
					MTP_double(location.lon)),
				MTP_long(location.access),
				MTP_int(location.width),
				MTP_int(location.height),
				MTP_int(location.zoom),
				MTP_int(location.scale)),
			MTP_int(offset),
			MTP_int(limit)
		)).done([=](const MTPupload_WebFile &result, mtpRequestId id) {
			webPartLoaded(result, id);
		}).fail([=](const RPCError &error, mtpRequestId id) {
			partFailed(error, id);
		}).toDC(shiftedDcId).send();
	}, [&](const StorageFileLocation &location) {
		const auto reference = location.fileReference();
		return api().request(MTPupload_GetFile(
			MTP_flags(0),
			location.tl(api().session().userId()),
			MTP_int(offset),
			MTP_int(limit)
		)).done([=](const MTPupload_File &result, mtpRequestId id) {
			normalPartLoaded(result, id);
		}).fail([=](const RPCError &error, mtpRequestId id) {
			normalPartFailed(reference, error, id);
		}).toDC(shiftedDcId).send();
	});
}

bool DownloadMtprotoTask::setWebFileSizeHook(int size) {
	return true;
}

void DownloadMtprotoTask::makeRequest(const RequestData &requestData) {
	placeSentRequest(sendRequest(requestData), requestData);
}

void DownloadMtprotoTask::requestMoreCdnFileHashes() {
	if (_cdnHashesRequestId || _cdnUncheckedParts.empty()) {
		return;
	}

	const auto requestData = _cdnUncheckedParts.cbegin()->first;
	const auto shiftedDcId = MTP::downloadDcId(
		dcId(),
		requestData.dcIndex);
	_cdnHashesRequestId = api().request(MTPupload_GetCdnFileHashes(
		MTP_bytes(_cdnToken),
		MTP_int(requestData.offset)
	)).done([=](const MTPVector<MTPFileHash> &result, mtpRequestId id) {
		getCdnFileHashesDone(result, id);
	}).fail([=](const RPCError &error, mtpRequestId id) {
		cdnPartFailed(error, id);
	}).toDC(shiftedDcId).send();
	placeSentRequest(_cdnHashesRequestId, requestData);
}

void DownloadMtprotoTask::normalPartLoaded(
		const MTPupload_File &result,
		mtpRequestId requestId) {
	const auto requestData = finishSentRequest(
		requestId,
		FinishRequestReason::Success);
	result.match([&](const MTPDupload_fileCdnRedirect &data) {
		switchToCDN(requestData, data);
	}, [&](const MTPDupload_file &data) {
		partLoaded(requestData.offset, data.vbytes().v);
	});
}

void DownloadMtprotoTask::webPartLoaded(
		const MTPupload_WebFile &result,
		mtpRequestId requestId) {
	result.match([&](const MTPDupload_webFile &data) {
		const auto requestData = finishSentRequest(
			requestId,
			FinishRequestReason::Success);
		if (setWebFileSizeHook(data.vsize().v)) {
			partLoaded(requestData.offset, data.vbytes().v);
		}
	});
}

void DownloadMtprotoTask::cdnPartLoaded(const MTPupload_CdnFile &result, mtpRequestId requestId) {
	result.match([&](const MTPDupload_cdnFileReuploadNeeded &data) {
		const auto requestData = finishSentRequest(
			requestId,
			FinishRequestReason::Redirect);
		const auto shiftedDcId = MTP::downloadDcId(
			dcId(),
			requestData.dcIndex);
		const auto requestId = api().request(MTPupload_ReuploadCdnFile(
			MTP_bytes(_cdnToken),
			data.vrequest_token()
		)).done([=](const MTPVector<MTPFileHash> &result, mtpRequestId id) {
			reuploadDone(result, id);
		}).fail([=](const RPCError &error, mtpRequestId id) {
			cdnPartFailed(error, id);
		}).toDC(shiftedDcId).send();
		placeSentRequest(requestId, requestData);
	}, [&](const MTPDupload_cdnFile &data) {
		const auto requestData = finishSentRequest(
			requestId,
			FinishRequestReason::Success);
		auto key = bytes::make_span(_cdnEncryptionKey);
		auto iv = bytes::make_span(_cdnEncryptionIV);
		Expects(key.size() == MTP::CTRState::KeySize);
		Expects(iv.size() == MTP::CTRState::IvecSize);

		auto state = MTP::CTRState();
		auto ivec = bytes::make_span(state.ivec);
		std::copy(iv.begin(), iv.end(), ivec.begin());

		auto counterOffset = static_cast<uint32>(requestData.offset) >> 4;
		state.ivec[15] = static_cast<uchar>(counterOffset & 0xFF);
		state.ivec[14] = static_cast<uchar>((counterOffset >> 8) & 0xFF);
		state.ivec[13] = static_cast<uchar>((counterOffset >> 16) & 0xFF);
		state.ivec[12] = static_cast<uchar>((counterOffset >> 24) & 0xFF);

		auto decryptInPlace = data.vbytes().v;
		auto buffer = bytes::make_detached_span(decryptInPlace);
		MTP::aesCtrEncrypt(buffer, key.data(), &state);

		switch (checkCdnFileHash(requestData.offset, buffer)) {
		case CheckCdnHashResult::NoHash: {
			_cdnUncheckedParts.emplace(requestData, decryptInPlace);
			requestMoreCdnFileHashes();
		} return;

		case CheckCdnHashResult::Invalid: {
			LOG(("API Error: Wrong cdnFileHash for offset %1."
				).arg(requestData.offset));
			cancelOnFail();
		} return;

		case CheckCdnHashResult::Good: {
			partLoaded(requestData.offset, decryptInPlace);
		} return;
		}
		Unexpected("Result of checkCdnFileHash()");
	});
}

DownloadMtprotoTask::CheckCdnHashResult DownloadMtprotoTask::checkCdnFileHash(
		int offset,
		bytes::const_span buffer) {
	const auto cdnFileHashIt = _cdnFileHashes.find(offset);
	if (cdnFileHashIt == _cdnFileHashes.cend()) {
		return CheckCdnHashResult::NoHash;
	}
	const auto realHash = openssl::Sha256(buffer);
	const auto receivedHash = bytes::make_span(cdnFileHashIt->second.hash);
	if (bytes::compare(realHash, receivedHash)) {
		return CheckCdnHashResult::Invalid;
	}
	return CheckCdnHashResult::Good;
}

void DownloadMtprotoTask::reuploadDone(
		const MTPVector<MTPFileHash> &result,
		mtpRequestId requestId) {
	const auto requestData = finishSentRequest(
		requestId,
		FinishRequestReason::Redirect);
	addCdnHashes(result.v);
	makeRequest(requestData);
}

void DownloadMtprotoTask::getCdnFileHashesDone(
		const MTPVector<MTPFileHash> &result,
		mtpRequestId requestId) {
	Expects(_cdnHashesRequestId == requestId);

	_cdnHashesRequestId = 0;

	const auto requestData = finishSentRequest(
		requestId,
		FinishRequestReason::Redirect);
	addCdnHashes(result.v);
	auto someMoreChecked = false;
	for (auto i = _cdnUncheckedParts.begin(); i != _cdnUncheckedParts.cend();) {
		const auto uncheckedData = i->first;
		const auto uncheckedBytes = bytes::make_span(i->second);

		switch (checkCdnFileHash(uncheckedData.offset, uncheckedBytes)) {
		case CheckCdnHashResult::NoHash: {
			++i;
		} break;

		case CheckCdnHashResult::Invalid: {
			LOG(("API Error: Wrong cdnFileHash for offset %1."
				).arg(uncheckedData.offset));
			cancelOnFail();
			return;
		} break;

		case CheckCdnHashResult::Good: {
			someMoreChecked = true;
			const auto goodOffset = uncheckedData.offset;
			const auto goodBytes = std::move(i->second);
			const auto weak = base::make_weak(this);
			i = _cdnUncheckedParts.erase(i);
			if (!feedPart(goodOffset, goodBytes) || !weak) {
				return;
			}
		} break;

		default: Unexpected("Result of checkCdnFileHash()");
		}
	}
	if (!someMoreChecked) {
		LOG(("API Error: "
			"Could not find cdnFileHash for offset %1 "
			"after getCdnFileHashes request."
			).arg(requestData.offset));
		cancelOnFail();
		return;
	}
	requestMoreCdnFileHashes();
}

void DownloadMtprotoTask::placeSentRequest(
		mtpRequestId requestId,
		const RequestData &requestData) {
	_owner->changeRequestedAmount(
		dcId(),
		requestData.dcIndex,
		Storage::kDownloadPartSize);
	const auto [i, ok1] = _sentRequests.emplace(requestId, requestData);
	const auto [j, ok2] = _requestByOffset.emplace(
		requestData.offset,
		requestId);

	Ensures(ok1 && ok2);
}

auto DownloadMtprotoTask::finishSentRequest(
	mtpRequestId requestId,
	FinishRequestReason reason)
-> RequestData {
	auto it = _sentRequests.find(requestId);
	Assert(it != _sentRequests.cend());

	const auto result = it->second;
	_owner->changeRequestedAmount(
		dcId(),
		result.dcIndex,
		-Storage::kDownloadPartSize);
	_sentRequests.erase(it);
	const auto ok = _requestByOffset.remove(result.offset);

	if (reason == FinishRequestReason::Success) {
		_owner->requestSucceeded(dcId(), result.dcIndex);
	}

	Ensures(ok);
	return result;
}

bool DownloadMtprotoTask::haveSentRequests() const {
	return !_sentRequests.empty() || !_cdnUncheckedParts.empty();
}

bool DownloadMtprotoTask::haveSentRequestForOffset(int offset) const {
	return _requestByOffset.contains(offset)
		|| _cdnUncheckedParts.contains({ offset, 0 });
}

void DownloadMtprotoTask::cancelAllRequests() {
	while (!_sentRequests.empty()) {
		cancelRequest(_sentRequests.begin()->first);
	}
	_cdnUncheckedParts.clear();
}

void DownloadMtprotoTask::cancelRequestForOffset(int offset) {
	const auto i = _requestByOffset.find(offset);
	if (i != end(_requestByOffset)) {
		cancelRequest(i->second);
	}
	_cdnUncheckedParts.remove({ offset, 0 });
}

void DownloadMtprotoTask::cancelRequest(mtpRequestId requestId) {
	api().request(requestId).cancel();
	[[maybe_unused]] const auto data = finishSentRequest(
		requestId,
		FinishRequestReason::Cancel);
}

void DownloadMtprotoTask::addToQueue() {
	_owner->enqueue(this);
}

void DownloadMtprotoTask::removeFromQueue() {
	_owner->remove(this);
}

void DownloadMtprotoTask::partLoaded(
		int offset,
		const QByteArray &bytes) {
	feedPart(offset, bytes);
}

bool DownloadMtprotoTask::normalPartFailed(
		QByteArray fileReference,
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}
	if (error.code() == 400
		&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
		api().refreshFileReference(
			_origin,
			this,
			requestId,
			fileReference);
		return true;
	}
	return partFailed(error, requestId);
}

bool DownloadMtprotoTask::partFailed(
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}
	cancelOnFail();
	return true;
}

bool DownloadMtprotoTask::cdnPartFailed(
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}

	if (requestId == _cdnHashesRequestId) {
		_cdnHashesRequestId = 0;
	}
	if (error.type() == qstr("FILE_TOKEN_INVALID")
		|| error.type() == qstr("REQUEST_TOKEN_INVALID")) {
		const auto requestData = finishSentRequest(
			requestId,
			FinishRequestReason::Redirect);
		changeCDNParams(
			requestData,
			0,
			QByteArray(),
			QByteArray(),
			QByteArray(),
			QVector<MTPFileHash>());
		return true;
	}
	return partFailed(error, requestId);
}

void DownloadMtprotoTask::switchToCDN(
		const RequestData &requestData,
		const MTPDupload_fileCdnRedirect &redirect) {
	changeCDNParams(
		requestData,
		redirect.vdc_id().v,
		redirect.vfile_token().v,
		redirect.vencryption_key().v,
		redirect.vencryption_iv().v,
		redirect.vfile_hashes().v);
}

void DownloadMtprotoTask::addCdnHashes(
		const QVector<MTPFileHash> &hashes) {
	for (const auto &hash : hashes) {
		hash.match([&](const MTPDfileHash &data) {
			_cdnFileHashes.emplace(
				data.voffset().v,
				CdnFileHash{ data.vlimit().v, data.vhash().v });
		});
	}
}

void DownloadMtprotoTask::changeCDNParams(
		const RequestData &requestData,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes) {
	if (dcId != 0
		&& (encryptionKey.size() != MTP::CTRState::KeySize
			|| encryptionIV.size() != MTP::CTRState::IvecSize)) {
		LOG(("Message Error: Wrong key (%1) / iv (%2) size in CDN params"
			).arg(encryptionKey.size()
			).arg(encryptionIV.size()));
		cancelOnFail();
		return;
	}

	auto resendAllRequests = (_cdnDcId != dcId
		|| _cdnToken != token
		|| _cdnEncryptionKey != encryptionKey
		|| _cdnEncryptionIV != encryptionIV);
	_cdnDcId = dcId;
	_cdnToken = token;
	_cdnEncryptionKey = encryptionKey;
	_cdnEncryptionIV = encryptionIV;
	addCdnHashes(hashes);

	if (resendAllRequests && !_sentRequests.empty()) {
		auto resendRequests = std::vector<RequestData>();
		resendRequests.reserve(_sentRequests.size());
		while (!_sentRequests.empty()) {
			const auto requestId = _sentRequests.begin()->first;
			api().request(requestId).cancel();
			resendRequests.push_back(finishSentRequest(
				requestId,
				FinishRequestReason::Redirect));
		}
		for (const auto &requestData : resendRequests) {
			makeRequest(requestData);
		}
	}
	makeRequest(requestData);
}

} // namespace Storage
