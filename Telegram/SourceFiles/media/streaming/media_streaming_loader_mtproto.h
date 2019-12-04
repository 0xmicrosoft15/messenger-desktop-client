/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_loader.h"
#include "mtproto/sender.h"
#include "data/data_file_origin.h"
#include "storage/file_download.h"

namespace Media {
namespace Streaming {

class LoaderMtproto
	: public Loader
	, public base::has_weak_ptr
	, public Storage::Downloader {
public:
	LoaderMtproto(
		not_null<Storage::DownloadManager*> owner,
		const StorageFileLocation &location,
		int size,
		Data::FileOrigin origin);
	~LoaderMtproto();

	[[nodiscard]] auto baseCacheKey() const
	-> std::optional<Storage::Cache::Key> override;
	[[nodiscard]] int size() const override;

	void load(int offset) override;
	void cancel(int offset) override;
	void increasePriority() override;
	void stop() override;

	// Parts will be sent from the main thread.
	[[nodiscard]] rpl::producer<LoadedPart> parts() const override;

	void attachDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader) override;
	void clearAttachedDownloader() override;

private:
	MTP::DcId dcId() const override;
	bool readyToRequest() const override;
	void loadPart(int dcIndex) override;

	void requestDone(int offset, const MTPupload_File &result);
	void requestFailed(
		int offset,
		const RPCError &error,
		const QByteArray &usedFileReference);
	void changeCdnParams(
		int offset,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes);
	void cancelForOffset(int offset);
	void changeRequestedAmount(int index, int amount);

	const not_null<Storage::DownloadManager*> _owner;

	// _location can be changed with an updated file_reference.
	StorageFileLocation _location;
	MTP::DcId _dcId = 0;

	const int _size = 0;
	const Data::FileOrigin _origin;

	MTP::Sender _api;

	PriorityQueue _requested;
	base::flat_map<int, mtpRequestId> _requests;
	base::flat_map<int, int> _amountByDcIndex;
	rpl::event_stream<LoadedPart> _parts;

	Storage::StreamedFileDownloader *_downloader = nullptr;

};

} // namespace Streaming
} // namespace Media
