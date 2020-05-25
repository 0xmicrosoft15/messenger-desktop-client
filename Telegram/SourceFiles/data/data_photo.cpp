/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_photo.h"

#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_reply_preview.h"
#include "data/data_photo_media.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "storage/file_download.h"
#include "core/application.h"
#include "facades.h"
#include "app.h"

namespace {

using Data::PhotoMedia;
using Data::PhotoSize;
using Data::PhotoSizeIndex;
using Data::kPhotoSizeCount;

} // namespace

PhotoData::PhotoData(not_null<Data::Session*> owner, PhotoId id)
: id(id)
, _owner(owner) {
}

PhotoData::~PhotoData() {
	for (auto &image : _images) {
		base::take(image.loader).reset();
	}
}

Data::Session &PhotoData::owner() const {
	return *_owner;
}

Main::Session &PhotoData::session() const {
	return _owner->session();
}

void PhotoData::automaticLoadSettingsChanged() {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	if (!(_images[index].flags & ImageFlag::Cancelled)) {
		return;
	}
	_images[index].loader = nullptr;
	_images[index].flags &= ~ImageFlag::Cancelled;
}

void PhotoData::load(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	load(PhotoSize::Large, origin, fromCloud, autoLoading);
}

bool PhotoData::loading() const {
	return loading(PhotoSize::Large);
}

bool PhotoData::loading(PhotoSize size) const {
	return (_images[PhotoSizeIndex(size)].loader != nullptr);
}

bool PhotoData::failed(PhotoSize size) const {
	return (_images[PhotoSizeIndex(size)].flags & ImageFlag::Failed);
}

const ImageLocation &PhotoData::location(PhotoSize size) const {
	return _images[PhotoSizeIndex(size)].location;
}

int PhotoData::imageByteSize(PhotoSize size) const {
	return _images[PhotoSizeIndex(size)].byteSize;
}

bool PhotoData::displayLoading() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return _images[index].loader
		? (!_images[index].loader->loadingLocal()
			|| !_images[index].loader->autoLoading())
		: (uploading() && !waitingForAlbum());
}

void PhotoData::cancel() {
	if (!loading()) {
		return;
	}

	const auto index = PhotoSizeIndex(PhotoSize::Large);
	_images[index].flags |= ImageFlag::Cancelled;
	destroyLoader(PhotoSize::Large);
	_owner->photoLoadDone(this);
}

float64 PhotoData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			const auto result = float64(uploadingData->offset)
				/ uploadingData->size;
			return snap(result, 0., 1.);
		}
		return 0.;
	}
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return loading() ? _images[index].loader->currentProgress() : 0.;
}

bool PhotoData::cancelled() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return (_images[index].flags & ImageFlag::Cancelled);
}

void PhotoData::setWaitingForAlbum() {
	if (uploading()) {
		uploadingData->waitingForAlbum = true;
	}
}

bool PhotoData::waitingForAlbum() const {
	return uploading() && uploadingData->waitingForAlbum;
}

int32 PhotoData::loadOffset() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return loading() ? _images[index].loader->currentOffset() : 0;
}

bool PhotoData::uploading() const {
	return (uploadingData != nullptr);
}

void PhotoData::unload() {
	_replyPreview = nullptr;
}

Image *PhotoData::getReplyPreview(Data::FileOrigin origin) {
	if (!_replyPreview) {
		_replyPreview = std::make_unique<Data::ReplyPreview>(this);
	}
	return _replyPreview->image(origin);
}

void PhotoData::setRemoteLocation(
		int32 dc,
		uint64 access,
		const QByteArray &fileReference) {
	_fileReference = fileReference;
	if (_dc != dc || _access != access) {
		_dc = dc;
		_access = access;
	}
}

MTPInputPhoto PhotoData::mtpInput() const {
	return MTP_inputPhoto(
		MTP_long(id),
		MTP_long(_access),
		MTP_bytes(_fileReference));
}

QByteArray PhotoData::fileReference() const {
	return _fileReference;
}

void PhotoData::refreshFileReference(const QByteArray &value) {
	_fileReference = value;
	for (auto &image : _images) {
		image.location.refreshFileReference(value);
	}
}

void PhotoData::collectLocalData(not_null<PhotoData*> local) {
	if (local == this) {
		return;
	}

	for (auto i = 0; i != kPhotoSizeCount; ++i) {
		if (const auto from = local->_images[i].location.file().cacheKey()) {
			if (const auto to = _images[i].location.file().cacheKey()) {
				_owner->cache().copyIfEmpty(from, to);
			}
		}
	}
	if (const auto localMedia = local->activeMediaView()) {
		const auto media = createMediaView();
		media->collectLocalData(localMedia.get());

		// Keep DocumentMedia alive for some more time.
		// NB! This allows DocumentMedia to outlive Main::Session!
		// In case this is a problem this code should be rewritten.
		crl::on_main(&session(), [media] {});
	}
}

bool PhotoData::isNull() const {
	return !_images[PhotoSizeIndex(PhotoSize::Large)].location.valid();
}

void PhotoData::load(
		PhotoSize size,
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	const auto index = PhotoSizeIndex(size);
	auto &image = _images[index];
	if (image.loader) {
		if (fromCloud == LoadFromCloudOrLocal) {
			image.loader->permitLoadFromCloud();
		}
		return;
	} else if ((image.flags & ImageFlag::Failed)
		|| !image.location.valid()) {
		return;
	} else if (const auto active = activeMediaView()) {
		if (active->image(size)) {
			return;
		}
	}
	image.flags &= ~ImageFlag::Cancelled;
	image.loader = CreateFileLoader(
		image.location.file(),
		origin,
		QString(),
		image.byteSize,
		UnknownFileLocation,
		LoadToCacheAsWell,
		fromCloud,
		autoLoading,
		Data::kImageCacheTag);

	image.loader->updates(
	) | rpl::start_with_next_error_done([=] {
		if (size == PhotoSize::Large) {
			_owner->photoLoadProgress(this);
		}
	}, [=, &image](bool started) {
		finishLoad(size);
		image.flags |= ImageFlag::Failed;
		if (size == PhotoSize::Large) {
			_owner->photoLoadFail(this, started);
		}
	}, [=, &image] {
		finishLoad(size);
		if (size == PhotoSize::Large) {
			_owner->photoLoadDone(this);
		}
	}, image.loader->lifetime());

	image.loader->start();

	if (size == PhotoSize::Large) {
		_owner->notifyPhotoLayoutChanged(this);
	}
}

void PhotoData::finishLoad(PhotoSize size) {
	const auto index = PhotoSizeIndex(size);
	auto &image = _images[index];

	// NB! image.loader may be in ~FileLoader() already.
	const auto guard = gsl::finally([&] {
		destroyLoader(size);
	});
	if (!image.loader || image.loader->cancelled()) {
		image.flags |= ImageFlag::Cancelled;
		return;
	} else if (auto read = image.loader->imageData(); read.isNull()) {
		image.flags |= ImageFlag::Failed;
	} else if (const auto active = activeMediaView()) {
		active->set(size, std::move(read));
	}
}

void PhotoData::destroyLoader(PhotoSize size) {
	const auto index = PhotoSizeIndex(size);
	auto &image = _images[index];

	// NB! image.loader may be in ~FileLoader() already.
	if (!image.loader) {
		return;
	}
	const auto loader = base::take(image.loader);
	if (image.flags & ImageFlag::Cancelled) {
		loader->cancel();
	}
}

std::shared_ptr<PhotoMedia> PhotoData::createMediaView() {
	if (auto result = activeMediaView()) {
		return result;
	}
	auto result = std::make_shared<PhotoMedia>(this);
	_media = result;
	return result;
}

std::shared_ptr<PhotoMedia> PhotoData::activeMediaView() const {
	return _media.lock();
}

void PhotoData::updateImages(
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large) {
	if (!inlineThumbnailBytes.isEmpty()
		&& _inlineThumbnailBytes.isEmpty()) {
		_inlineThumbnailBytes = inlineThumbnailBytes;
	}
	const auto update = [&](PhotoSize size, const ImageWithLocation &data) {
		auto &image = _images[PhotoSizeIndex(size)];
		if (data.location.valid()
			&& (!image.location.valid()
				|| image.location.width() != data.location.width()
				|| image.location.height() != data.location.height())) {
			image.location = data.location;
			image.byteSize = data.bytesCount;
			if (!data.preloaded.isNull()) {
				image.loader = nullptr;
				if (const auto media = activeMediaView()) {
					media->set(size, data.preloaded);
				}
			} else if (image.loader) {
				const auto origin = base::take(image.loader)->fileOrigin();
				load(size, origin);
			}
			if (!data.bytes.isEmpty()) {
				if (const auto cacheKey = image.location.file().cacheKey()) {
					owner().cache().putIfEmpty(
						cacheKey,
						Storage::Cache::Database::TaggedValue(
							base::duplicate(data.bytes),
							Data::kImageCacheTag));
				}
			}
		}
		//if (was->isDelayedStorageImage()) { // #TODO optimize
		//	if (const auto location = now->location(); location.valid()) {
		//		was->setDelayedStorageLocation(
		//			Data::FileOrigin(),
		//			location);
		//	}
		//}
	};
	update(PhotoSize::Small, small);
	update(PhotoSize::Thumbnail, thumbnail);
	update(PhotoSize::Large, large);
}

int PhotoData::width() const {
	return _images[PhotoSizeIndex(PhotoSize::Large)].location.width();
}

int PhotoData::height() const {
	return _images[PhotoSizeIndex(PhotoSize::Large)].location.height();
}

PhotoClickHandler::PhotoClickHandler(
	not_null<PhotoData*> photo,
	FullMsgId context,
	PeerData *peer)
: FileClickHandler(context)
, _session(&photo->session())
, _photo(photo)
, _peer(peer) {
}

void PhotoOpenClickHandler::onClickImpl() const {
	if (valid()) {
		Core::App().showPhoto(this);
	}
}

void PhotoSaveClickHandler::onClickImpl() const {
	if (!valid()) {
		return;
	}
	const auto data = photo();
	if (!data->date) {
		return;
	} else {
		data->load(context());
	}
}

void PhotoCancelClickHandler::onClickImpl() const {
	if (!valid()) {
		return;
	}
	const auto data = photo();
	if (!data->date) {
		return;
	} else if (data->uploading()) {
		if (const auto item = data->owner().message(context())) {
			App::main()->cancelUploadLayer(item);
		}
	} else {
		data->cancel();
	}
}
