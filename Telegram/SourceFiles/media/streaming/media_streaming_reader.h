/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_loader.h"
#include "base/bytes.h"
#include "base/weak_ptr.h"
#include "base/thread_safe_wrap.h"

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Data {
class Session;
} // namespace Data

namespace Media {
namespace Streaming {

class Loader;
struct LoadedPart;
enum class Error;

class Reader final : public base::has_weak_ptr {
public:
	// Main thread.
	Reader(not_null<Data::Session*> owner, std::unique_ptr<Loader> loader);

	// Any thread.
	[[nodiscard]] int size() const;
	[[nodiscard]] bool isRemoteLoader() const;

	// Single thread.
	[[nodiscard]] bool fill(
		int offset,
		bytes::span buffer,
		not_null<crl::semaphore*> notify);
	[[nodiscard]] std::optional<Error> streamingError() const;
	void headerDone();

	// Thread safe.
	void startSleep(not_null<crl::semaphore*> wake);
	void wakeFromSleep();
	void stopSleep();

	// Main thread.
	void startStreaming();
	void stopStreaming(bool stillActive = false);
	[[nodiscard]] rpl::producer<LoadedPart> partsForDownloader() const;
	void loadForDownloader(int offset);
	void doneForDownloader(int offset);
	void cancelForDownloader();

	~Reader();

private:
	static constexpr auto kLoadFromRemoteMax = 8;

	struct CacheHelper;

	using PartsMap = base::flat_map<int, QByteArray>;

	template <int Size>
	class StackIntVector {
	public:
		bool add(int value);
		auto values() const;

	private:
		std::array<int, Size> _storage = { -1 };

	};

	struct SerializedSlice {
		int number = -1;
		QByteArray data;
	};
	struct FillResult {
		static constexpr auto kReadFromCacheMax = 2;

		StackIntVector<kReadFromCacheMax> sliceNumbersFromCache;
		StackIntVector<kLoadFromRemoteMax> offsetsFromLoader;
		SerializedSlice toCache;
		bool filled = false;
	};

	struct Slice {
		enum class Flag : uchar {
			LoadingFromCache = 0x01,
			LoadedFromCache = 0x02,
			ChangedSinceCache = 0x04,
		};
		friend constexpr inline bool is_flag_type(Flag) { return true; }
		using Flags = base::flags<Flag>;

		struct PrepareFillResult {
			StackIntVector<kLoadFromRemoteMax> offsetsFromLoader;
			PartsMap::const_iterator start;
			PartsMap::const_iterator finish;
			bool ready = true;
		};

		void processCacheData(PartsMap &&data);
		void addPart(int offset, QByteArray bytes);
		PrepareFillResult prepareFill(int from, int till);

		// Get up to kLoadFromRemoteMax not loaded parts in from-till range.
		StackIntVector<kLoadFromRemoteMax> offsetsFromLoader(
			int from,
			int till) const;

		PartsMap parts;
		Flags flags;

	};

	class Slices {
	public:
		Slices(int size, bool useCache);

		void headerDone(bool fromCache);
		[[nodiscard]] bool headerWontBeFilled() const;
		[[nodiscard]] bool headerModeUnknown() const;
		[[nodiscard]] bool isFullInHeader() const;
		[[nodiscard]] bool isGoodHeader() const;

		void processCacheResult(int sliceNumber, PartsMap &&result);
		void processPart(int offset, QByteArray &&bytes);

		[[nodiscard]] FillResult fill(int offset, bytes::span buffer);
		[[nodiscard]] SerializedSlice unloadToCache();

		[[nodiscard]] QByteArray partForDownloader(int offset) const;
		[[nodiscard]] std::optional<int> readCacheRequiredFor(int offset);

	private:
		enum class HeaderMode {
			Unknown,
			Small,
			Good,
			Full,
			NoCache,
		};

		void applyHeaderCacheData();
		[[nodiscard]] int maxSliceSize(int sliceNumber) const;
		[[nodiscard]] SerializedSlice serializeAndUnloadSlice(
			int sliceNumber);
		[[nodiscard]] SerializedSlice serializeAndUnloadUnused();
		[[nodiscard]] QByteArray serializeComplexSlice(
			const Slice &slice) const;
		[[nodiscard]] QByteArray serializeAndUnloadFirstSliceNoHeader();
		void markSliceUsed(int sliceIndex);
		[[nodiscard]] bool computeIsGoodHeader() const;
		[[nodiscard]] FillResult fillFromHeader(
			int offset,
			bytes::span buffer);

		std::vector<Slice> _data;
		Slice _header;
		std::deque<int> _usedSlices;
		int _size = 0;
		HeaderMode _headerMode = HeaderMode::Unknown;

	};

	// 0 is for headerData, slice index = sliceNumber - 1.
	// returns false if asked for a known-empty downloader slice cache.
	void readFromCache(int sliceNumber);
	[[nodiscard]] bool readFromCacheForDownloader();
	bool processCacheResults();
	void putToCache(SerializedSlice &&data);

	void cancelLoadInRange(int from, int till);
	void loadAtOffset(int offset);
	void checkLoadWillBeFirst(int offset);
	bool processLoadedParts();

	bool checkForSomethingMoreReceived();

	bool fillFromSlices(int offset, bytes::span buffer);

	void finalizeCache();

	void processDownloaderRequests();
	void checkCacheResultsForDownloader();
	[[nodiscard]] bool downloaderWaitForCachedSlice(int offset);
	void enqueueDownloaderOffsets();
	void checkForDownloaderChange(int checkItemsCount);
	void checkForDownloaderReadyOffsets();

	static std::shared_ptr<CacheHelper> InitCacheHelper(
		std::optional<Storage::Cache::Key> baseKey);

	const not_null<Data::Session*> _owner;
	const std::unique_ptr<Loader> _loader;
	const std::shared_ptr<CacheHelper> _cacheHelper;

	base::thread_safe_queue<LoadedPart, std::vector> _loadedParts;
	std::atomic<crl::semaphore*> _waiting = nullptr;
	std::atomic<crl::semaphore*> _sleeping = nullptr;
	PriorityQueue _loadingOffsets;

	Slices _slices;

	// Even if streaming had failed, the Reader can work for the downloader.
	std::optional<Error> _streamingError;

	std::atomic<bool> _downloaderAttached = false;
	base::thread_safe_queue<int> _downloaderOffsetRequests;
	std::deque<int> _offsetsForDownloader;
	base::flat_set<int> _downloaderOffsetsRequested;
	int _downloaderSliceNumber = 0; // > 0 means we want it from cache.
	std::optional<PartsMap> _downloaderSliceCache;

	// Main thread.
	rpl::event_stream<LoadedPart> _partsForDownloader;
	bool _streamingActive = false;
	rpl::lifetime _lifetime;

};

} // namespace Streaming
} // namespace Media
