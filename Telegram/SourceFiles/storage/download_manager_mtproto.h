/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_file_origin.h"
#include "base/timer.h"
#include "base/weak_ptr.h"

class ApiWrap;
class RPCError;

namespace Storage {

// Different part sizes are not supported for now :(
// Because we start downloading with some part size
// and then we get a CDN-redirect where we support only
// fixed part size download for hash checking.
constexpr auto kDownloadPartSize = 128 * 1024;

class DownloadMtprotoTask;

class DownloadManagerMtproto final : public base::has_weak_ptr {
public:
	using Task = DownloadMtprotoTask;

	explicit DownloadManagerMtproto(not_null<ApiWrap*> api);
	~DownloadManagerMtproto();

	[[nodiscard]] ApiWrap &api() const {
		return *_api;
	}

	void enqueue(not_null<Task*> task);
	void remove(not_null<Task*> task);

	[[nodiscard]] base::Observable<void> &taskFinished() {
		return _taskFinishedObservable;
	}

	void requestedAmountIncrement(MTP::DcId dcId, int index, int amount);
	[[nodiscard]] int chooseDcIndexForRequest(MTP::DcId dcId);

private:
	class Queue final {
	public:
		void enqueue(not_null<Task*> task);
		void remove(not_null<Task*> task);
		void resetGeneration();
		[[nodiscard]] bool empty() const;
		[[nodiscard]] Task *nextTask() const;

	private:
		std::vector<not_null<Task*>> _tasks;
		std::vector<not_null<Task*>> _previousGeneration;

	};

	void checkSendNext();
	void checkSendNext(MTP::DcId dcId, Queue &queue);

	void killDownloadSessionsStart(MTP::DcId dcId);
	void killDownloadSessionsStop(MTP::DcId dcId);
	void killDownloadSessions();

	void resetGeneration();

	const not_null<ApiWrap*> _api;

	base::Observable<void> _taskFinishedObservable;

	base::flat_map<MTP::DcId, std::vector<int>> _requestedBytesAmount;
	base::Timer _resetGenerationTimer;

	base::flat_map<MTP::DcId, crl::time> _killDownloadSessionTimes;
	base::Timer _killDownloadSessionsTimer;

	base::flat_map<MTP::DcId, Queue> _queues;

};

class DownloadMtprotoTask : public base::has_weak_ptr {
public:
	struct Location {
		base::variant<
			StorageFileLocation,
			WebFileLocation,
			GeoPointLocation> data;
	};

	DownloadMtprotoTask(
		not_null<DownloadManagerMtproto*> owner,
		const StorageFileLocation &location,
		Data::FileOrigin origin);
	DownloadMtprotoTask(
		not_null<DownloadManagerMtproto*> owner,
		MTP::DcId dcId,
		const Location &location);
	virtual ~DownloadMtprotoTask();

	[[nodiscard]] MTP::DcId dcId() const;
	[[nodiscard]] Data::FileOrigin fileOrigin() const;
	[[nodiscard]] uint64 objectId() const;
	[[nodiscard]] const Location &location() const;

	[[nodiscard]] virtual bool readyToRequest() const = 0;
	void loadPart(int dcIndex);

	void refreshFileReferenceFrom(
		const Data::UpdatedFileReferences &updates,
		int requestId,
		const QByteArray &current);

protected:
	[[nodiscard]] bool haveSentRequests() const;
	[[nodiscard]] bool haveSentRequestForOffset(int offset) const;
	void cancelAllRequests();
	void cancelRequestForOffset(int offset);

	void addToQueue();
	void removeFromQueue();

	[[nodiscard]] ApiWrap &api() const {
		return _owner->api();
	}

private:
	struct RequestData {
		int offset = 0;
		int dcIndex = 0;

		inline bool operator<(const RequestData &other) const {
			return offset < other.offset;
		}
	};
	struct CdnFileHash {
		CdnFileHash(int limit, QByteArray hash) : limit(limit), hash(hash) {
		}
		int limit = 0;
		QByteArray hash;
	};
	enum class CheckCdnHashResult {
		NoHash,
		Invalid,
		Good,
	};

	// Called only if readyToRequest() == true.
	[[nodiscard]] virtual int takeNextRequestOffset() = 0;
	virtual bool feedPart(int offset, const QByteArray &bytes) = 0;
	virtual bool setWebFileSizeHook(int size);
	virtual void cancelOnFail() = 0;

	void cancelRequest(mtpRequestId requestId);
	void makeRequest(const RequestData &requestData);
	void normalPartLoaded(
		const MTPupload_File &result,
		mtpRequestId requestId);
	void webPartLoaded(
		const MTPupload_WebFile &result,
		mtpRequestId requestId);
	void cdnPartLoaded(
		const MTPupload_CdnFile &result,
		mtpRequestId requestId);
	void reuploadDone(
		const MTPVector<MTPFileHash> &result,
		mtpRequestId requestId);
	void requestMoreCdnFileHashes();
	void getCdnFileHashesDone(
		const MTPVector<MTPFileHash> &result,
		mtpRequestId requestId);

	void partLoaded(int offset, const QByteArray &bytes);

	bool partFailed(const RPCError &error, mtpRequestId requestId);
	bool normalPartFailed(
		QByteArray fileReference,
		const RPCError &error,
		mtpRequestId requestId);
	bool cdnPartFailed(const RPCError &error, mtpRequestId requestId);

	[[nodiscard]] mtpRequestId sendRequest(const RequestData &requestData);
	void placeSentRequest(
		mtpRequestId requestId,
		const RequestData &requestData);
	[[nodiscard]] RequestData finishSentRequest(mtpRequestId requestId);
	void switchToCDN(
		const RequestData &requestData,
		const MTPDupload_fileCdnRedirect &redirect);
	void addCdnHashes(const QVector<MTPFileHash> &hashes);
	void changeCDNParams(
		const RequestData &requestData,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes);

	[[nodiscard]] CheckCdnHashResult checkCdnFileHash(
		int offset,
		bytes::const_span buffer);

	const not_null<DownloadManagerMtproto*> _owner;
	const MTP::DcId _dcId = 0;

	// _location can be changed with an updated file_reference.
	Location _location;
	const Data::FileOrigin _origin;

	base::flat_map<mtpRequestId, RequestData> _sentRequests;
	base::flat_map<int, mtpRequestId> _requestByOffset;

	MTP::DcId _cdnDcId = 0;
	QByteArray _cdnToken;
	QByteArray _cdnEncryptionKey;
	QByteArray _cdnEncryptionIV;
	base::flat_map<int, CdnFileHash> _cdnFileHashes;
	base::flat_map<RequestData, QByteArray> _cdnUncheckedParts;
	mtpRequestId _cdnHashesRequestId = 0;

};

} // namespace Storage
