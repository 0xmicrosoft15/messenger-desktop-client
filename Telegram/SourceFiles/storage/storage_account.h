/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "storage/cache/storage_cache_database.h"
#include "data/stickers/data_stickers_set.h"

class History;
class FileLocation;

namespace Export {
struct Settings;
} // namespace Export

namespace Main {
class Account;
class Settings;
} // namespace Main

namespace Data {
class WallPaper;
} // namespace Data

namespace MTP {
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Storage {
namespace details {
struct ReadSettingsContext;
} // namespace details

class EncryptionKey;

using FileKey = quint64;

enum class StartResult : uchar {
	Success,
	IncorrectPasscode,
};

struct MessageDraft {
	MsgId msgId = 0;
	TextWithTags textWithTags;
	bool previewCancelled = false;
};

class Account final {
public:
	Account(not_null<Main::Account*> owner, const QString &dataName);
	~Account();

	[[nodiscard]] StartResult start(const QByteArray &passcode);
	[[nodiscard]] int oldMapVersion() const {
		return _oldMapVersion;
	}

	[[nodiscard]] bool checkPasscode(const QByteArray &passcode) const;
	void setPasscode(const QByteArray &passcode);

	void writeSettings();
	void writeMtpData();

	void writeBackground(const Data::WallPaper &paper, const QImage &image);
	bool readBackground();

	void writeDrafts(
		const PeerId &peer,
		const MessageDraft &localDraft,
		const MessageDraft &editDraft);
	void readDraftsWithCursors(not_null<History*> history);
	void writeDraftCursors(
		const PeerId &peer,
		const MessageCursor &localCursor,
		const MessageCursor &editCursor);
	[[nodiscard]] bool hasDraftCursors(const PeerId &peer);
	[[nodiscard]] bool hasDraft(const PeerId &peer);

	void writeFileLocation(MediaKey location, const FileLocation &local);
	[[nodiscard]] FileLocation readFileLocation(MediaKey location);
	void removeFileLocation(MediaKey location);

	[[nodiscard]] EncryptionKey cacheKey() const;
	[[nodiscard]] QString cachePath() const;
	[[nodiscard]] Cache::Database::Settings cacheSettings() const;
	void updateCacheSettings(
		Cache::Database::SettingsUpdate &update,
		Cache::Database::SettingsUpdate &updateBig);

	[[nodiscard]] EncryptionKey cacheBigFileKey() const;
	[[nodiscard]] QString cacheBigFilePath() const;
	[[nodiscard]] Cache::Database::Settings cacheBigFileSettings() const;

	void writeInstalledStickers();
	void writeFeaturedStickers();
	void writeRecentStickers();
	void writeFavedStickers();
	void writeArchivedStickers();
	void readInstalledStickers();
	void readFeaturedStickers();
	void readRecentStickers();
	void readFavedStickers();
	void readArchivedStickers();
	void writeSavedGifs();
	void readSavedGifs();

	void writeRecentHashtagsAndBots();
	void readRecentHashtagsAndBots();
	void saveRecentSentHashtags(const QString &text);
	void saveRecentSearchHashtags(const QString &text);

	void writeExportSettings(const Export::Settings &settings);
	[[nodiscard]] Export::Settings readExportSettings();

	void writeSelf();
	void readSelf(const QByteArray &serialized, int32 streamVersion);

	void markBotTrusted(not_null<UserData*> bot);
	[[nodiscard]] bool isBotTrusted(not_null<UserData*> bot);

	[[nodiscard]] bool encrypt(
		const void *src,
		void *dst,
		uint32 len,
		const void *key128) const;
	[[nodiscard]] bool decrypt(
		const void *src,
		void *dst,
		uint32 len,
		const void *key128) const;

	void reset();

private:
	enum class ReadMapResult {
		Success,
		IncorrectPasscode,
		Failed,
	};

	[[nodiscard]] base::flat_set<QString> collectGoodNames() const;
	[[nodiscard]] auto prepareReadSettingsContext() const
		-> details::ReadSettingsContext;

	[[nodiscard]] ReadMapResult readMap(const QByteArray &passcode);
	void writeMapDelayed();
	void writeMapQueued();
	void writeMap();

	void readLocations();
	void writeLocations();
	void writeLocationsQueued();
	void writeLocationsDelayed();

	std::unique_ptr<Main::Settings> readSettings();
	void writeSettings(Main::Settings *stored);
	bool readOldUserSettings(
		bool remove,
		details::ReadSettingsContext &context);
	void readOldUserSettingsFields(
		QIODevice *device,
		qint32 &version,
		details::ReadSettingsContext &context);
	void readOldMtpDataFields(
		QIODevice *device,
		qint32 &version,
		details::ReadSettingsContext &context);
	bool readOldMtpData(bool remove, details::ReadSettingsContext &context);

	void readMtpData();
	std::unique_ptr<Main::Settings> applyReadContext(
		details::ReadSettingsContext &&context);

	[[nodiscard]] QByteArray serializeCallSettings();
	void deserializeCallSettings(QByteArray &settings);

	void readDraftCursors(
		const PeerId &peer,
		MessageCursor &localCursor,
		MessageCursor &editCursor);
	void clearDraftCursors(const PeerId &peer);

	void writeStickerSet(
		QDataStream &stream,
		const Data::StickersSet &set);
	template <typename CheckSet>
	void writeStickerSets(
		FileKey &stickersKey,
		CheckSet checkSet,
		const Data::StickersSetsOrder &order);
	void readStickerSets(
		FileKey &stickersKey,
		Data::StickersSetsOrder *outOrder = nullptr,
		MTPDstickerSet::Flags readingFlags = 0);
	void importOldRecentStickers();

	void readTrustedBots();
	void writeTrustedBots();

	std::optional<RecentHashtagPack> saveRecentHashtags(
		Fn<RecentHashtagPack()> getPack,
		const QString &text);

	const not_null<Main::Account*> _owner;
	const QString _dataName;
	const FileKey _dataNameKey = 0;
	const QString _basePath;
	const QString _databasePath;

	MTP::AuthKeyPtr _localKey;
	MTP::AuthKeyPtr _passcodeKey;
	QByteArray _passcodeKeySalt;
	QByteArray _passcodeKeyEncrypted;

	base::flat_map<PeerId, FileKey> _draftsMap;
	base::flat_map<PeerId, FileKey> _draftCursorsMap;
	base::flat_map<PeerId, bool> _draftsNotReadMap;

	QMultiMap<MediaKey, FileLocation> _fileLocations;
	QMap<QString, QPair<MediaKey, FileLocation>> _fileLocationPairs;
	QMap<MediaKey, MediaKey> _fileLocationAliases;

	FileKey _locationsKey = 0;
	FileKey _trustedBotsKey = 0;
	FileKey _installedStickersKey = 0;
	FileKey _featuredStickersKey = 0;
	FileKey _recentStickersKey = 0;
	FileKey _favedStickersKey = 0;
	FileKey _archivedStickersKey = 0;
	FileKey _savedGifsKey = 0;
	FileKey _recentStickersKeyOld = 0;
	FileKey _backgroundKeyDay = 0;
	FileKey _backgroundKeyNight = 0;
	FileKey _settingsKey = 0;
	FileKey _recentHashtagsAndBotsKey = 0;
	FileKey _exportSettingsKey = 0;

	qint64 _cacheTotalSizeLimit = 0;
	qint64 _cacheBigFileTotalSizeLimit = 0;
	qint32 _cacheTotalTimeLimit = 0;
	qint32 _cacheBigFileTotalTimeLimit = 0;

	base::flat_set<uint64> _trustedBots;
	bool _trustedBotsRead = false;
	bool _backgroundCanWrite = true;
	bool _readingUserSettings = false;
	bool _recentHashtagsAndBotsWereRead = false;

	int _oldMapVersion = 0;

	base::Timer _writeMapTimer;
	base::Timer _writeLocationsTimer;
	bool _mapChanged = false;
	bool _locationsChanged = false;

};

} // namespace Storage
