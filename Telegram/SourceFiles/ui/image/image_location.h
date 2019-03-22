/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class FileLoader;

enum LoadFromCloudSetting {
	LoadFromCloudOrLocal,
	LoadFromLocalOnly,
};

enum LoadToCacheSetting {
	LoadToFileOnly,
	LoadToCacheAsWell,
};

using InMemoryKey = std::pair<uint64, uint64>;

class StorageFileLocation {
public:
	// Those are used in serialization, don't change.
	enum class Type : uchar {
		General         = 0x00,
		Encrypted       = 0x01,
		Document        = 0x02,
		Secure          = 0x03,
		Takeout         = 0x04,
		Photo           = 0x05,
		PeerPhoto       = 0x06,
		StickerSetThumb = 0x07,
	};

	StorageFileLocation() = default;
	StorageFileLocation(MTP::DcId dcId, const MTPInputFileLocation &tl);

	[[nodiscard]] MTP::DcId dcId() const;
	[[nodiscard]] MTPInputFileLocation tl() const;

	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] static std::optional<StorageFileLocation> FromSerialized(
		const QByteArray &serialized);

	[[nodiscard]] bool valid() const;
	[[nodiscard]] InMemoryKey inMemoryKey() const;

	[[nodiscard]] QByteArray fileReference() const;
	bool refreshFileReference(const QByteArray &data) {
		if (data.isEmpty() || _fileReference == data) {
			return false;
		}
		_fileReference = data;
		return true;
	}

private:
	friend bool operator==(
		const StorageFileLocation &a,
		const StorageFileLocation &b);

	uint16 _dcId = 0;
	Type _type = Type::General;
	uint8 _sizeLetter = 0;
	int32 _localId = 0;
	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint64 _volumeId = 0;
	QByteArray _fileReference;

};

inline bool operator!=(
		const StorageFileLocation &a,
		const StorageFileLocation &b) {
	return !(a == b);
}

class StorageImageLocation {
public:
	StorageImageLocation() = default;
	StorageImageLocation(
		const StorageFileLocation &file,
		int width,
		int height);

	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] static std::optional<StorageImageLocation> FromSerialized(
		const QByteArray &serialized);

	[[nodiscard]] const StorageFileLocation &file() const {
		return _file;
	}
	[[nodiscard]] int width() const {
		return _width;
	}
	[[nodiscard]] int height() const {
		return _height;
	}

	void setSize(int width, int height) {
		_width = width;
		_height = height;
	}

	[[nodiscard]] bool valid() const {
		return _file.valid();
	}
	[[nodiscard]] InMemoryKey inMemoryKey() const {
		return _file.inMemoryKey();
	}
	[[nodiscard]] QByteArray fileReference() const {
		return _file.fileReference();
	}
	bool refreshFileReference(const QByteArray &data) {
		return _file.refreshFileReference(data);
	}

private:
	friend inline bool operator==(
			const StorageImageLocation &a,
			const StorageImageLocation &b) {
		return (a._file == b._file);
	}

	StorageFileLocation _file;
	int _width = 0;
	int _height = 0;

};

inline bool operator!=(
		const StorageImageLocation &a,
		const StorageImageLocation &b) {
	return !(a == b);
}

class WebFileLocation {
public:
	WebFileLocation() = default;
	WebFileLocation(int32 dc, const QByteArray &url, uint64 accessHash)
	: _accessHash(accessHash)
	, _url(url)
	, _dc(dc) {
	}
	bool isNull() const {
		return !_dc;
	}
	int32 dc() const {
		return _dc;
	}
	uint64 accessHash() const {
		return _accessHash;
	}
	const QByteArray &url() const {
		return _url;
	}

	static WebFileLocation Null;

private:
	uint64 _accessHash = 0;
	QByteArray _url;
	int32 _dc = 0;

	friend inline bool operator==(
			const WebFileLocation &a,
			const WebFileLocation &b) {
		return (a._dc == b._dc)
			&& (a._accessHash == b._accessHash)
			&& (a._url == b._url);
	}

};

inline bool operator!=(const WebFileLocation &a, const WebFileLocation &b) {
	return !(a == b);
}

struct GeoPointLocation {
	float64 lat = 0.;
	float64 lon = 0.;
	uint64 access = 0;
	int32 width = 0;
	int32 height = 0;
	int32 zoom = 0;
	int32 scale = 0;
};

inline bool operator==(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return (a.lat == b.lat)
		&& (a.lon == b.lon)
		&& (a.access == b.access)
		&& (a.width == b.width)
		&& (a.height == b.height)
		&& (a.zoom == b.zoom)
		&& (a.scale == b.scale);
}

inline bool operator!=(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return !(a == b);
}

class Image;
class ImagePtr {
public:
	ImagePtr();
	explicit ImagePtr(not_null<Image*> data);

	Image *operator->() const;
	Image *get() const;

	explicit operator bool() const;

private:
	not_null<Image*> _data;

};

inline InMemoryKey inMemoryKey(const StorageFileLocation &location) {
	return location.inMemoryKey();
}

inline InMemoryKey inMemoryKey(const StorageImageLocation &location) {
	return location.inMemoryKey();
}

inline InMemoryKey inMemoryKey(const WebFileLocation &location) {
	auto result = InMemoryKey();
	const auto &url = location.url();
	const auto sha = hashSha1(url.data(), url.size());
	bytes::copy(
		bytes::object_as_span(&result),
		bytes::make_span(sha).subspan(0, sizeof(result)));
	result.first |= (uint64(uint16(location.dc())) << 56);
	return result;
}

inline InMemoryKey inMemoryKey(const GeoPointLocation &location) {
	return InMemoryKey(
		(uint64(std::round(std::abs(location.lat + 360.) * 1000000)) << 32)
		| uint64(std::round(std::abs(location.lon + 360.) * 1000000)),
		(uint64(location.width) << 32) | uint64(location.height));
}

inline QSize shrinkToKeepAspect(int32 width, int32 height, int32 towidth, int32 toheight) {
	int32 w = qMax(width, 1), h = qMax(height, 1);
	if (w * toheight > h * towidth) {
		h = qRound(h * towidth / float64(w));
		w = towidth;
	} else {
		w = qRound(w * toheight / float64(h));
		h = toheight;
	}
	return QSize(qMax(w, 1), qMax(h, 1));
}

class PsFileBookmark;
class ReadAccessEnabler {
public:
	ReadAccessEnabler(const PsFileBookmark *bookmark);
	ReadAccessEnabler(const std::shared_ptr<PsFileBookmark> &bookmark);
	bool failed() const {
		return _failed;
	}
	~ReadAccessEnabler();

private:
	const PsFileBookmark *_bookmark;
	bool _failed;

};

class FileLocation {
public:
	FileLocation() = default;
	explicit FileLocation(const QString &name);

	bool check() const;
	const QString &name() const;
	void setBookmark(const QByteArray &bookmark);
	QByteArray bookmark() const;
	bool isEmpty() const {
		return name().isEmpty();
	}

	bool accessEnable() const;
	void accessDisable() const;

	QString fname;
	QDateTime modified;
	qint32 size;

private:
	std::shared_ptr<PsFileBookmark> _bookmark;

};
inline bool operator==(const FileLocation &a, const FileLocation &b) {
	return (a.name() == b.name()) && (a.modified == b.modified) && (a.size == b.size);
}
inline bool operator!=(const FileLocation &a, const FileLocation &b) {
	return !(a == b);
}
