/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_file_click_handler.h"

#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "mainwidget.h" // App::main

FileClickHandler::FileClickHandler(
	not_null<Main::Session*> session,
	FullMsgId context)
: _session(session)
, _context(context) {
}

Main::Session &FileClickHandler::session() const {
	return *_session;
}

void FileClickHandler::setMessageId(FullMsgId context) {
	_context = context;
}

FullMsgId FileClickHandler::context() const {
	return _context;
}

HistoryItem *FileClickHandler::getActionItem() const {
	return _session->data().message(context());
}

not_null<DocumentData*> DocumentClickHandler::document() const {
	return _document;
}

DocumentWrappedClickHandler::DocumentWrappedClickHandler(
	ClickHandlerPtr wrapped,
	not_null<DocumentData*> document,
	FullMsgId context)
: DocumentClickHandler(document, context)
, _wrapped(wrapped) {
}

void DocumentWrappedClickHandler::onClickImpl() const {
	_wrapped->onClick({ Qt::LeftButton });
}

DocumentClickHandler::DocumentClickHandler(
	not_null<DocumentData*> document,
	FullMsgId context)
: FileClickHandler(&document->session(), context)
, _document(document) {
}

DocumentOpenClickHandler::DocumentOpenClickHandler(
	not_null<DocumentData*> document,
	Fn<void(FullMsgId)> &&callback,
	FullMsgId context)
: DocumentClickHandler(document, context)
, _handler(std::move(callback)) {
	Expects(_handler != nullptr);
}

void DocumentOpenClickHandler::onClickImpl() const {
	_handler(context());
}

void DocumentSaveClickHandler::Save(
		Data::FileOrigin origin,
		not_null<DocumentData*> data,
		Mode mode) {
	if (!data->date) {
		return;
	}

	auto savename = QString();
	if (mode != Mode::ToCacheOrFile || !data->saveToCache()) {
		if (mode != Mode::ToNewFile && data->saveFromData()) {
			return;
		}
		const auto filepath = data->filepath(true);
		const auto fileinfo = QFileInfo(
			);
		const auto filedir = filepath.isEmpty()
			? QDir()
			: fileinfo.dir();
		const auto filename = filepath.isEmpty()
			? QString()
			: fileinfo.fileName();
		savename = DocumentFileNameForSave(
			data,
			(mode == Mode::ToNewFile),
			filename,
			filedir);
		if (savename.isEmpty()) {
			return;
		}
	}
	data->save(origin, savename);
}

void DocumentSaveClickHandler::onClickImpl() const {
	Save(context(), document());
}

void DocumentCancelClickHandler::onClickImpl() const {
	const auto data = document();
	if (!data->date) {
		return;
	} else if (data->uploading()) {
		if (const auto item = data->owner().message(context())) {
			if (const auto m = App::main()) { // multi good
				if (&m->session() == &data->session()) {
					m->cancelUploadLayer(item);
				}
			}
		}
	} else {
		data->cancel();
	}
}

void DocumentOpenWithClickHandler::Open(
		Data::FileOrigin origin,
		not_null<DocumentData*> data) {
	if (!data->date) {
		return;
	}

	data->saveFromDataSilent();
	const auto path = data->filepath(true);
	if (!path.isEmpty()) {
		File::OpenWith(path, QCursor::pos());
	} else {
		DocumentSaveClickHandler::Save(
			origin,
			data,
			DocumentSaveClickHandler::Mode::ToFile);
	}
}

void DocumentOpenWithClickHandler::onClickImpl() const {
	Open(context(), document());
}

PhotoClickHandler::PhotoClickHandler(
	not_null<PhotoData*> photo,
	FullMsgId context,
	PeerData *peer)
: FileClickHandler(&photo->session(), context)
, _photo(photo)
, _peer(peer) {
}

not_null<PhotoData*> PhotoClickHandler::photo() const {
	return _photo;
}

PeerData *PhotoClickHandler::peer() const {
	return _peer;
}

PhotoOpenClickHandler::PhotoOpenClickHandler(
	not_null<PhotoData*> photo,
	Fn<void(FullMsgId)> &&callback,
	FullMsgId context)
: PhotoClickHandler(photo, context)
, _handler(std::move(callback)) {
	Expects(_handler != nullptr);
}

void PhotoOpenClickHandler::onClickImpl() const {
	_handler(context());
}

void PhotoSaveClickHandler::onClickImpl() const {
	const auto data = photo();
	if (!data->date) {
		return;
	} else {
		data->clearFailed(Data::PhotoSize::Large);
		data->load(context());
	}
}

void PhotoCancelClickHandler::onClickImpl() const {
	const auto data = photo();
	if (!data->date) {
		return;
	} else if (data->uploading()) {
		if (const auto item = data->owner().message(context())) {
			if (const auto m = App::main()) { // multi good
				if (&m->session() == &data->session()) {
					m->cancelUploadLayer(item);
				}
			}
		}
	} else {
		data->cancel();
	}
}
