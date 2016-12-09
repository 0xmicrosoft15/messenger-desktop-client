/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "settings/settings_cover.h"

#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "observer_peer.h"
#include "lang.h"
#include "application.h"
#include "apiwrap.h"
#include "profile/profile_userpic_button.h"
#include "profile/profile_cover_drop_area.h"
#include "boxes/confirmbox.h"
#include "boxes/photocropbox.h"
#include "boxes/addcontactbox.h"
#include "styles/style_settings.h"
#include "styles/style_profile.h" // for divider
#include "platform/platform_file_dialog.h"

namespace Settings {

CoverWidget::CoverWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, QString())
, _self(App::self())
, _userpicButton(this, _self)
, _name(this, st::settingsNameLabel)
, _editNameInline(this, st::settingsEditButton)
, _setPhoto(this, lang(lng_settings_upload), st::settingsPrimaryButton)
, _editName(this, lang(lng_settings_edit), st::settingsSecondaryButton) {
	setAcceptDrops(true);

	_name->setSelectable(true);
	_name->setContextCopyText(lang(lng_profile_copy_fullname));

	_setPhoto->setClickedCallback(App::LambdaDelayed(st::settingsPrimaryButton.ripple.hideDuration, this, [this] { onSetPhoto(); }));
	connect(_editName, SIGNAL(clicked()), this, SLOT(onEditName()));
	connect(_editNameInline, SIGNAL(clicked()), this, SLOT(onEditName()));

	auto observeEvents = Notify::PeerUpdate::Flag::NameChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));
	subscribe(FileDialog::QueryDone(), [this](const FileDialog::QueryUpdate &update) {
		notifyFileQueryUpdated(update);
	});

	connect(App::app(), SIGNAL(peerPhotoDone(PeerId)), this, SLOT(onPhotoUploadStatusChanged(PeerId)));
	connect(App::app(), SIGNAL(peerPhotoFail(PeerId)), this, SLOT(onPhotoUploadStatusChanged(PeerId)));

	connect(_userpicButton, SIGNAL(clicked()), this, SLOT(onPhotoShow()));
	validatePhoto();

	refreshNameText();

	subscribe(Global::RefConnectionTypeChanged(), [this] { refreshStatusText(); });
	refreshStatusText();
}

PhotoData *CoverWidget::validatePhoto() const {
	PhotoData *photo = (_self->photoId && _self->photoId != UnknownPeerPhotoId) ? App::photo(_self->photoId) : nullptr;
	if ((_self->photoId == UnknownPeerPhotoId) || (_self->photoId && (!photo || !photo->date))) {
		App::api()->requestFullPeer(_self);
		return nullptr;
	}
	return photo;
}

void CoverWidget::onPhotoShow() {
	if (auto photo = validatePhoto()) {
		App::wnd()->showPhoto(photo, _self);
	}
}

void CoverWidget::onCancelPhotoUpload() {
	if (auto app = App::app()) {
		app->cancelPhotoUpdate(_self->id);
		refreshStatusText();
	}
}

int CoverWidget::resizeGetHeight(int newWidth) {
	int newHeight = 0;

	newHeight += st::settingsMarginTop;

	_userpicButton->moveToLeft(contentLeft() + st::settingsPhotoLeft, newHeight, newWidth);

	int infoLeft = _userpicButton->x() + _userpicButton->width();
	_statusPosition = QPoint(infoLeft + st::settingsStatusLeft, _userpicButton->y() + st::settingsStatusTop);
	if (_cancelPhotoUpload) {
		_cancelPhotoUpload->moveToLeft(_statusPosition.x() + st::settingsStatusFont->width(_statusText) + st::settingsStatusFont->spacew, _statusPosition.y(), newWidth);
	}

	int buttonLeft = _userpicButton->x() + _userpicButton->width() + st::settingsButtonLeft;
	int buttonsRight = newWidth - st::settingsButtonSkip;
	_setPhoto->moveToLeft(buttonLeft, _userpicButton->y() + st::settingsButtonTop, newWidth);
	buttonLeft += _setPhoto->width() + st::settingsButtonSkip;
	_editName->moveToLeft(buttonLeft, _setPhoto->y(), newWidth);
	_editNameVisible = (buttonLeft + _editName->width() + st::settingsButtonSkip <= newWidth);
	_editName->setVisible(_editNameVisible);

	refreshNameGeometry(newWidth);

	newHeight += st::settingsPhotoSize;
	newHeight += st::settingsMarginBottom;

	_dividerTop = newHeight;
	newHeight += st::profileDividerLeft.height();

	newHeight += st::settingsBlocksTop;

	resizeDropArea();
	return newHeight;
}

void CoverWidget::refreshNameGeometry(int newWidth) {
	int infoLeft = _userpicButton->x() + _userpicButton->width();
	int nameLeft = infoLeft + st::settingsNameLeft - st::settingsNameLabel.margin.left();
	int nameTop = _userpicButton->y() + st::settingsNameTop - st::settingsNameLabel.margin.top();
	int nameWidth = newWidth - infoLeft - st::settingsNameLeft;
	auto editNameInlineVisible = !_editNameVisible;
	if (editNameInlineVisible) {
		nameWidth -= _editNameInline->width();
	}
	int marginsAdd = st::settingsNameLabel.margin.left() + st::settingsNameLabel.margin.right();

	_name->resizeToWidth(qMin(nameWidth - marginsAdd, _name->naturalWidth()) + marginsAdd);
	_name->moveToLeft(nameLeft, nameTop, newWidth);

	_editNameInline->moveToLeft(nameLeft + _name->width(), nameTop, newWidth);
	_editNameInline->setVisible(editNameInlineVisible);
}

void CoverWidget::showFinished() {
	_userpicButton->showFinished();
}

void CoverWidget::paintContents(Painter &p) {
	p.setFont(st::settingsStatusFont);
	p.setPen(_statusTextIsOnline ? st::settingsStatusFgActive : st::settingsStatusFg);
	p.drawTextLeft(_statusPosition.x(), _statusPosition.y(), width(), _statusText);

	paintDivider(p);
}

void CoverWidget::resizeDropArea() {
	if (_dropArea) {
		_dropArea->setGeometry(0, 0, width(), _dividerTop);
	}
}

void CoverWidget::dropAreaHidden(Profile::CoverDropArea *dropArea) {
	if (_dropArea == dropArea) {
		_dropArea.destroyDelayed();
	}
}

bool CoverWidget::mimeDataHasImage(const QMimeData *mimeData) const {
	if (!mimeData) return false;

	if (mimeData->hasImage()) return true;

	auto uriListFormat = qsl("text/uri-list");
	if (!mimeData->hasFormat(uriListFormat)) return false;

	const auto &urls = mimeData->urls();
	if (urls.size() != 1) return false;

	auto &url = urls.at(0);
	if (!url.isLocalFile()) return false;

	auto file = Platform::FileDialog::UrlToLocal(url);

	QFileInfo info(file);
	if (info.isDir()) return false;

	if (info.size() > App::kImageSizeLimit) return false;

	for (auto &ext : cImgExtensions()) {
		if (file.endsWith(ext, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

void CoverWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (!mimeDataHasImage(e->mimeData())) {
		e->ignore();
		return;
	}
	if (!_dropArea) {
		auto title = lang(lng_profile_drop_area_title);
		auto subtitle = lang(lng_settings_drop_area_subtitle);
		_dropArea = new Profile::CoverDropArea(this, title, subtitle);
		resizeDropArea();
	}
	_dropArea->showAnimated();
	e->setDropAction(Qt::CopyAction);
	e->accept();
}

void CoverWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_dropArea && !_dropArea->hiding()) {
		_dropArea->hideAnimated([this](Profile::CoverDropArea *area) { dropAreaHidden(area); });
	}
}

void CoverWidget::dropEvent(QDropEvent *e) {
	auto mimeData = e->mimeData();

	QImage img;
	if (mimeData->hasImage()) {
		img = qvariant_cast<QImage>(mimeData->imageData());
	} else {
		auto urls = mimeData->urls();
		if (urls.size() == 1) {
			auto &url = urls.at(0);
			if (url.isLocalFile()) {
				img = App::readImage(Platform::FileDialog::UrlToLocal(url));
			}
		}
	}

	if (!_dropArea->hiding()) {
		_dropArea->hideAnimated([this](Profile::CoverDropArea *area) { dropAreaHidden(area); });
	}
	e->acceptProposedAction();

	showSetPhotoBox(img);
}

void CoverWidget::paintDivider(Painter &p) {
	auto dividerHeight = st::profileDividerLeft.height();
	auto divider = rtlrect(0, _dividerTop, width(), dividerHeight, width());
	p.fillRect(divider, st::profileDividerBg);
	auto dividerFillTop = rtlrect(0, _dividerTop, width(), st::profileDividerTop.height(), width());
	st::profileDividerTop.fill(p, dividerFillTop);
	auto dividerFillBottom = rtlrect(0, _dividerTop + dividerHeight - st::profileDividerBottom.height(), width(), st::profileDividerBottom.height(), width());
	st::profileDividerBottom.fill(p, dividerFillBottom);
}

void CoverWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != _self) {
		return;
	}
	if (update.flags & Notify::PeerUpdate::Flag::NameChanged) {
		refreshNameText();
	}
	//if (update.flags & UpdateFlag::UserOnlineChanged) { // TODO
	//	refreshStatusText();
	//}
}

void CoverWidget::refreshNameText() {
	_name->setText(App::peerName(_self));
	refreshNameGeometry(width());
}

void CoverWidget::refreshStatusText() {
	if (auto app = App::app()) {
		if (app->isPhotoUpdating(_self->id)) {
			_statusText = lang(lng_settings_uploading_photo);
			_statusTextIsOnline = false;
			if (!_cancelPhotoUpload) {
				_cancelPhotoUpload.create(this, lang(lng_cancel), st::defaultLinkButton);
				connect(_cancelPhotoUpload, SIGNAL(clicked()), this, SLOT(onCancelPhotoUpload()));
				_cancelPhotoUpload->show();
				_cancelPhotoUpload->moveToLeft(_statusPosition.x() + st::settingsStatusFont->width(_statusText) + st::settingsStatusFont->spacew, _statusPosition.y());
			}
			update();
			return;
		}
	}

	_cancelPhotoUpload.destroy();
	auto state = MTP::dcstate();
	if (state == MTP::ConnectingState || state == MTP::DisconnectedState || state < 0) {
		_statusText = lang(lng_status_connecting);
		_statusTextIsOnline = false;
	} else {
		_statusText = lang(lng_status_online);
		_statusTextIsOnline = true;
	}
	update();
}

void CoverWidget::onSetPhoto() {
	auto imageExtensions = cImgExtensions();
	auto filter = qsl("Image files (*") + imageExtensions.join(qsl(" *")) + qsl(");;") + filedialogAllFilesFilter();

	_setPhotoFileQueryId = FileDialog::queryReadFile(lang(lng_choose_image), filter);
}

void CoverWidget::onEditName() {
	Ui::showLayer(new EditNameTitleBox(self()));
}

void CoverWidget::notifyFileQueryUpdated(const FileDialog::QueryUpdate &update) {
	if (_setPhotoFileQueryId != update.queryId) {
		return;
	}
	_setPhotoFileQueryId = 0;

	if (update.filePaths.isEmpty() && update.remoteContent.isEmpty()) {
		return;
	}

	QImage img;
	if (!update.remoteContent.isEmpty()) {
		img = App::readImage(update.remoteContent);
	} else {
		img = App::readImage(update.filePaths.front());
	}

	showSetPhotoBox(img);
}

void CoverWidget::showSetPhotoBox(const QImage &img) {
	if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
		Ui::showLayer(new InformBox(lang(lng_bad_photo)));
		return;
	}

	auto box = new PhotoCropBox(img, _self);
	connect(box, SIGNAL(closed(LayerWidget*)), this, SLOT(onPhotoUploadStatusChanged()));
	Ui::showLayer(box);
}

void CoverWidget::onPhotoUploadStatusChanged(PeerId peerId) {
	if (!peerId || peerId == _self->id) {
		refreshStatusText();
	}
}

} // namespace Settings
