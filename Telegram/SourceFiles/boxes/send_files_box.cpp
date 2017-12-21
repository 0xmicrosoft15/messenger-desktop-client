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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/send_files_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "storage/storage_media_prepare.h"
#include "mainwidget.h"
#include "history/history_media_types.h"
#include "core/file_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/grouped_layout.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"
#include "media/media_clip_reader.h"
#include "window/window_controller.h"

namespace {

constexpr auto kMinPreviewWidth = 20;

} // namespace

SendFilesBox::SendFilesBox(
	QWidget*,
	Storage::PreparedList &&list,
	CompressConfirm compressed)
: _list(std::move(list))
, _compressConfirm(compressed)
, _caption(
	this,
	st::confirmCaptionArea,
	langFactory(_list.files.size() > 1
		? lng_photos_comment
		: lng_photo_caption)) {
	if (_list.files.size() == 1) {
		prepareSingleFileLayout();
	}
}

void SendFilesBox::prepareSingleFileLayout() {
	Expects(_list.files.size() == 1);

	const auto &file = _list.files[0];
	auto preview = QImage();
	if (const auto image = base::get_if<FileMediaInformation::Image>(
			&file.information->media)) {
		preview = image->data;
		_animated = image->animated;
	} else if (const auto video = base::get_if<FileMediaInformation::Video>(
			&file.information->media)) {
		preview = video->thumbnail;
		_animated = true;
	}

	if (!Storage::ValidateThumbDimensions(preview.width(), preview.height())
		|| _animated) {
		_compressConfirm = CompressConfirm::None;
	}

	if (!preview.isNull()) {
		if (!_animated && _compressConfirm == CompressConfirm::None) {
			auto originalWidth = preview.width();
			auto originalHeight = preview.height();
			auto thumbWidth = st::msgFileThumbSize;
			if (originalWidth > originalHeight) {
				thumbWidth = (originalWidth * st::msgFileThumbSize)
					/ originalHeight;
			}
			auto options = Images::Option::Smooth
				| Images::Option::RoundedSmall
				| Images::Option::RoundedTopLeft
				| Images::Option::RoundedTopRight
				| Images::Option::RoundedBottomLeft
				| Images::Option::RoundedBottomRight;
			_fileThumb = Images::pixmap(
				preview,
				thumbWidth * cIntRetinaFactor(),
				0,
				options,
				st::msgFileThumbSize,
				st::msgFileThumbSize);
		} else {
			auto maxW = 0;
			auto maxH = 0;
			if (_animated) {
				auto limitW = st::sendMediaPreviewSize;
				auto limitH = st::confirmMaxHeight;
				maxW = qMax(preview.width(), 1);
				maxH = qMax(preview.height(), 1);
				if (maxW * limitH > maxH * limitW) {
					if (maxW < limitW) {
						maxH = maxH * limitW / maxW;
						maxW = limitW;
					}
				} else {
					if (maxH < limitH) {
						maxW = maxW * limitH / maxH;
						maxH = limitH;
					}
				}
				preview = Images::prepare(
					preview,
					maxW * cIntRetinaFactor(),
					maxH * cIntRetinaFactor(),
					Images::Option::Smooth | Images::Option::Blurred,
					maxW,
					maxH);
			}
			auto originalWidth = preview.width();
			auto originalHeight = preview.height();
			if (!originalWidth || !originalHeight) {
				originalWidth = originalHeight = 1;
			}
			_previewWidth = st::sendMediaPreviewSize;
			if (preview.width() < _previewWidth) {
				_previewWidth = qMax(preview.width(), kMinPreviewWidth);
			}
			auto maxthumbh = qMin(qRound(1.5 * _previewWidth), st::confirmMaxHeight);
			_previewHeight = qRound(originalHeight * float64(_previewWidth) / originalWidth);
			if (_previewHeight > maxthumbh) {
				_previewWidth = qRound(_previewWidth * float64(maxthumbh) / _previewHeight);
				accumulate_max(_previewWidth, kMinPreviewWidth);
				_previewHeight = maxthumbh;
			}
			_previewLeft = (st::boxWideWidth - _previewWidth) / 2;

			preview = std::move(preview).scaled(
				_previewWidth * cIntRetinaFactor(),
				_previewHeight * cIntRetinaFactor(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
			preview = Images::prepareOpaque(std::move(preview));
			_preview = App::pixmapFromImageInPlace(std::move(preview));
			_preview.setDevicePixelRatio(cRetinaFactor());

			prepareGifPreview();
		}
	}
	if (_preview.isNull()) {
		prepareDocumentLayout();
	}
}

void SendFilesBox::prepareGifPreview() {
	using namespace Media::Clip;
	auto createGifPreview = [this] {
		const auto &information = _list.files.front().information;
		if (!information) {
			return false;
		}
		if (const auto video = base::get_if<FileMediaInformation::Video>(
				&information->media)) {
			return video->isGifv;
		}
		// Plain old .gif animation.
		return _animated;
	};
	if (createGifPreview()) {
		const auto callback = [this](Notification notification) {
			clipCallback(notification);
		};
		_gifPreview = Media::Clip::MakeReader(
			_list.files.front().path,
			callback);
		if (_gifPreview) _gifPreview->setAutoplay();
	}
}

void SendFilesBox::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gifPreview && _gifPreview->state() == State::Error) {
			_gifPreview.setBad();
		}

		if (_gifPreview && _gifPreview->ready() && !_gifPreview->started()) {
			auto s = QSize(_previewWidth, _previewHeight);
			_gifPreview->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gifPreview && !_gifPreview->currentDisplayed()) {
			update();
		}
	} break;
	}
}

void SendFilesBox::prepareDocumentLayout() {
	const auto &file = _list.files.front();
	const auto filepath = file.path;
	if (filepath.isEmpty()) {
		const auto data = base::get_if<FileMediaInformation::Image>(
			&file.information->media);
		const auto image = data ? data->data : QImage();
		auto filename = filedialogDefaultName(qsl("image"), qsl(".png"), QString(), true);
		_nameText.setText(st::semiboldTextStyle, filename, _textNameOptions);
		_statusText = qsl("%1x%2").arg(image.width()).arg(image.height());
		_statusWidth = qMax(_nameText.maxWidth(), st::normalFont->width(_statusText));
		_fileIsImage = true;
	} else {
		auto fileinfo = QFileInfo(filepath);
		auto filename = fileinfo.fileName();
		_fileIsImage = fileIsImage(filename, mimeTypeForFile(fileinfo).name());

		auto songTitle = QString();
		auto songPerformer = QString();
		if (file.information) {
			if (const auto song = base::get_if<FileMediaInformation::Song>(
					&file.information->media)) {
				songTitle = song->title;
				songPerformer = song->performer;
				_fileIsAudio = true;
			}
		}

		const auto nameString = DocumentData::ComposeNameString(
			filename,
			songTitle,
			songPerformer);
		_nameText.setText(
			st::semiboldTextStyle,
			nameString,
			_textNameOptions);
		_statusText = formatSizeText(fileinfo.size());
		_statusWidth = qMax(
			_nameText.maxWidth(),
			st::normalFont->width(_statusText));
	}
}

void SendFilesBox::prepare() {
	Expects(controller() != nullptr);

	if (_list.files.size() > 1) {
		updateTitleText();
	}

	_send = addButton(langFactory(lng_send_button), [this] { send(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	if (_compressConfirm != CompressConfirm::None) {
		auto compressed = (_compressConfirm == CompressConfirm::Auto) ? cCompressPastedImage() : (_compressConfirm == CompressConfirm::Yes);
		auto text = lng_send_images_compress(lt_count, _list.files.size());
		_compressed.create(this, text, compressed, st::defaultBoxCheckbox);
		subscribe(_compressed->checkedChanged, [this](bool checked) {
			compressedChange();
		});
	}
	if (_caption) {
		_caption->setMaxLength(MaxPhotoCaption);
		_caption->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
		connect(_caption, &Ui::InputArea::resized, this, [this] {
			captionResized();
		});
		connect(_caption, &Ui::InputArea::submitted, this, [this](
				bool ctrlShiftEnter) {
			send(ctrlShiftEnter);
		});
		connect(_caption, &Ui::InputArea::cancelled, this, [this] {
			closeBox();
		});
	}
	subscribe(boxClosing, [this] {
		if (!_confirmed && _cancelledCallback) {
			_cancelledCallback();
		}
	});
	_send->setText(getSendButtonText());
	updateButtonsGeometry();
	updateBoxSize();
}

base::lambda<QString()> SendFilesBox::getSendButtonText() const {
	if (_compressed && _compressed->checked()) {
		return [count = _list.files.size()] {
			return lng_send_photos(lt_count, count);
		};
	}
	return [count = _list.files.size()] {
		return lng_send_files(lt_count, count);
	};
}

void SendFilesBox::compressedChange() {
	setInnerFocus();
	_send->setText(getSendButtonText());
	updateButtonsGeometry();
	updateControlsGeometry();
}

void SendFilesBox::captionResized() {
	updateBoxSize();
	updateControlsGeometry();
	update();
}

void SendFilesBox::updateTitleText() {
	_titleText = (_compressConfirm == CompressConfirm::None)
		? lng_send_files_selected(lt_count, _list.files.size())
		: lng_send_images_selected(lt_count, _list.files.size());
	update();
}

void SendFilesBox::updateBoxSize() {
	auto newHeight = _titleText.isEmpty() ? 0 : st::boxTitleHeight;
	if (!_preview.isNull()) {
		newHeight += st::boxPhotoPadding.top() + _previewHeight;
	} else if (!_fileThumb.isNull()) {
		newHeight += st::boxPhotoPadding.top() + st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else if (_list.files.size() > 1) {
		newHeight += 0;
	} else {
		newHeight += st::boxPhotoPadding.top() + st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (_compressed) {
		newHeight += st::boxPhotoCompressedSkip + _compressed->heightNoMargins();
	}
	if (_caption) {
		newHeight += st::boxPhotoCaptionSkip + _caption->height();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void SendFilesBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		const auto modifiers = e->modifiers();
		const auto ctrl = modifiers.testFlag(Qt::ControlModifier)
			|| modifiers.testFlag(Qt::MetaModifier);
		const auto shift = modifiers.testFlag(Qt::ShiftModifier);
		send(ctrl && shift);
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void SendFilesBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (!_titleText.isEmpty()) {
		p.setFont(st::boxPhotoTitleFont);
		p.setPen(st::boxTitleFg);
		p.drawTextLeft(st::boxPhotoTitlePosition.x(), st::boxPhotoTitlePosition.y(), width(), _titleText);
	}

	if (!_preview.isNull()) {
		if (_previewLeft > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _previewLeft - st::boxPhotoPadding.left(), _previewHeight, st::confirmBg);
		}
		if (_previewLeft + _previewWidth < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_previewLeft + _previewWidth, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _previewLeft - _previewWidth, _previewHeight, st::confirmBg);
		}
		if (_gifPreview && _gifPreview->started()) {
			auto s = QSize(_previewWidth, _previewHeight);
			auto paused = controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
			auto frame = _gifPreview->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : getms());
			p.drawPixmap(_previewLeft, st::boxPhotoPadding.top(), frame);
		} else {
			p.drawPixmap(_previewLeft, st::boxPhotoPadding.top(), _preview);
		}
		if (_animated && !_gifPreview) {
			auto inner = QRect(_previewLeft + (_previewWidth - st::msgFileSize) / 2, st::boxPhotoPadding.top() + (_previewHeight - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgDateImgBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			auto icon = &st::historyFileInPlay;
			icon->paintInCenter(p, inner);
		}
	} else if (_list.files.size() < 2) {
		auto w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		auto h = _fileThumb.isNull() ? (st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom()) : (st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom());
		auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
		if (_fileThumb.isNull()) {
			nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
			nametop = st::msgFileNameTop;
			nameright = st::msgFilePadding.left();
			statustop = st::msgFileStatusTop;
		} else {
			nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
			nametop = st::msgFileThumbNameTop;
			nameright = st::msgFileThumbPadding.left();
			statustop = st::msgFileThumbStatusTop;
			linktop = st::msgFileThumbLinkTop;
		}
		auto namewidth = w - nameleft - (_fileThumb.isNull() ? st::msgFilePadding.left() : st::msgFileThumbPadding.left());
		int32 x = (width() - w) / 2, y = st::boxPhotoPadding.top();

		App::roundRect(p, x, y, w, h, st::msgOutBg, MessageOutCorners, &st::msgOutShadow);

		if (_fileThumb.isNull()) {
			QRect inner(rtlrect(x + st::msgFilePadding.left(), y + st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, width()));
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgFileOutBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			auto &icon = _fileIsAudio ? st::historyFileOutPlay : _fileIsImage ? st::historyFileOutImage : st::historyFileOutDocument;
			icon.paintInCenter(p, inner);
		} else {
			QRect rthumb(rtlrect(x + st::msgFileThumbPadding.left(), y + st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, width()));
			p.drawPixmap(rthumb.topLeft(), _fileThumb);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::historyFileNameOutFg);
		_nameText.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

		auto &status = st::mediaOutFg;
		p.setFont(st::normalFont);
		p.setPen(status);
		p.drawTextLeft(x + nameleft, y + statustop, width(), _statusText);
	}
}

void SendFilesBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	updateControlsGeometry();
}

void SendFilesBox::updateControlsGeometry() {
	auto bottom = height();
	if (_caption) {
		_caption->resize(st::sendMediaPreviewSize, _caption->height());
		_caption->moveToLeft(st::boxPhotoPadding.left(), bottom - _caption->height());
		bottom -= st::boxPhotoCaptionSkip + _caption->height();
	}
	if (_compressed) {
		_compressed->moveToLeft(st::boxPhotoPadding.left(), bottom - _compressed->heightNoMargins());
		bottom -= st::boxPhotoCompressedSkip + _compressed->heightNoMargins();
	}
}

void SendFilesBox::setInnerFocus() {
	if (!_caption || _caption->isHidden()) {
		setFocus();
	} else {
		_caption->setFocusFast();
	}
}

void SendFilesBox::send(bool ctrlShiftEnter) {
	if (_compressed && _compressConfirm == CompressConfirm::Auto && _compressed->checked() != cCompressPastedImage()) {
		cSetCompressPastedImage(_compressed->checked());
		Local::writeUserSettings();
	}
	_confirmed = true;
	if (_confirmedCallback) {
		auto compressed = _compressed ? _compressed->checked() : false;
		auto caption = _caption ? TextUtilities::PrepareForSending(_caption->getLastText(), TextUtilities::PrepareTextOption::CheckLinks) : QString();
		_confirmedCallback(
			std::move(_list),
			compressed,
			caption,
			ctrlShiftEnter);
	}
	closeBox();
}

SendFilesBox::~SendFilesBox() = default;

struct SendAlbumBox::Thumb {
	Ui::GroupMediaLayout layout;
	QPixmap image;
};

SendAlbumBox::SendAlbumBox(QWidget*, Storage::PreparedList &&list)
: _list(std::move(list))
, _caption(
	this,
	st::confirmCaptionArea,
	langFactory(_list.files.size() > 1
		? lng_photos_comment
		: lng_photo_caption)) {
}

void SendAlbumBox::prepare() {
	Expects(controller() != nullptr);

	prepareThumbs();

	addButton(langFactory(lng_send_button), [this] { send(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	if (_caption) {
		_caption->setMaxLength(MaxPhotoCaption);
		_caption->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
		connect(_caption, &Ui::InputArea::resized, this, [this] {
			captionResized();
		});
		connect(_caption, &Ui::InputArea::submitted, this, [this](
				bool ctrlShiftEnter) {
			send(ctrlShiftEnter);
		});
		connect(_caption, &Ui::InputArea::cancelled, this, [this] {
			closeBox();
		});
	}
	subscribe(boxClosing, [this] {
		if (!_confirmed && _cancelledCallback) {
			_cancelledCallback();
		}
	});

	updateButtonsGeometry();
	updateBoxSize();
}

void SendAlbumBox::prepareThumbs() {
	auto sizes = ranges::view::all(
		_list.files
	) | ranges::view::transform([](const Storage::PreparedFile &file) {
		return file.preview.size() / cIntRetinaFactor();
	}) | ranges::to_vector;

	const auto count = int(sizes.size());
	const auto layout = Ui::LayoutMediaGroup(
		sizes,
		st::sendMediaPreviewSize,
		st::historyGroupWidthMin / 2,
		st::historyGroupSkip / 2);
	Assert(layout.size() == count);

	_thumbs.reserve(count);
	for (auto i = 0; i != count; ++i) {
		_thumbs.push_back(prepareThumb(_list.files[i].preview, layout[i]));
		const auto &geometry = layout[i].geometry;
		accumulate_max(_thumbsHeight, geometry.y() + geometry.height());
	}
}

SendAlbumBox::Thumb SendAlbumBox::prepareThumb(
		const QImage &preview,
		const Ui::GroupMediaLayout &layout) const {
	auto result = Thumb();
	result.layout = layout;

	const auto width = layout.geometry.width();
	const auto height = layout.geometry.height();
	const auto corners = Ui::GetCornersFromSides(layout.sides);
	using Option = Images::Option;
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| ((corners & RectPart::TopLeft) ? Option::RoundedTopLeft : Option::None)
		| ((corners & RectPart::TopRight) ? Option::RoundedTopRight : Option::None)
		| ((corners & RectPart::BottomLeft) ? Option::RoundedBottomLeft : Option::None)
		| ((corners & RectPart::BottomRight) ? Option::RoundedBottomRight : Option::None);
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ preview.width(), preview.height() },
		{ width, height });
	const auto pixWidth = pixSize.width() * cIntRetinaFactor();
	const auto pixHeight = pixSize.height() * cIntRetinaFactor();

	result.image = App::pixmapFromImageInPlace(Images::prepare(
		preview,
		pixWidth,
		pixHeight,
		options,
		width,
		height));
	return result;
}

void SendAlbumBox::captionResized() {
	updateBoxSize();
	updateControlsGeometry();
	update();
}

void SendAlbumBox::updateBoxSize() {
	auto newHeight = st::boxPhotoPadding.top() + _thumbsHeight;
	if (_caption) {
		newHeight += st::boxPhotoCaptionSkip + _caption->height();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void SendAlbumBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		const auto modifiers = e->modifiers();
		const auto ctrl = modifiers.testFlag(Qt::ControlModifier)
			|| modifiers.testFlag(Qt::MetaModifier);
		const auto shift = modifiers.testFlag(Qt::ShiftModifier);
		send(ctrl && shift);
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void SendAlbumBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	const auto left = (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
	const auto top = st::boxPhotoPadding.top();
	for (const auto &thumb : _thumbs) {
		p.drawPixmap(
			left + thumb.layout.geometry.x(),
			top + thumb.layout.geometry.y(),
			thumb.image);
	}
}

void SendAlbumBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	updateControlsGeometry();
}

void SendAlbumBox::updateControlsGeometry() {
	auto bottom = height();
	if (_caption) {
		_caption->resize(st::sendMediaPreviewSize, _caption->height());
		_caption->moveToLeft(st::boxPhotoPadding.left(), bottom - _caption->height());
		bottom -= st::boxPhotoCaptionSkip + _caption->height();
	}
}

void SendAlbumBox::setInnerFocus() {
	if (!_caption || _caption->isHidden()) {
		setFocus();
	} else {
		_caption->setFocusFast();
	}
}

void SendAlbumBox::send(bool ctrlShiftEnter) {
	_confirmed = true;
	if (_confirmedCallback) {
		auto caption = _caption
			? TextUtilities::PrepareForSending(
				_caption->getLastText(),
				TextUtilities::PrepareTextOption::CheckLinks)
			: QString();
		_confirmedCallback(
			std::move(_list),
			caption,
			ctrlShiftEnter);
	}
	closeBox();
}

SendAlbumBox::~SendAlbumBox() = default;

EditCaptionBox::EditCaptionBox(QWidget*, HistoryMedia *media, FullMsgId msgId) : _msgId(msgId) {
	Expects(media->canEditCaption());

	QSize dimensions;
	ImagePtr image;
	QString caption;
	DocumentData *doc = nullptr;

	switch (media->type()) {
	case MediaTypeGif: {
		_animated = true;
		doc = static_cast<HistoryGif*>(media)->getDocument();
		dimensions = doc->dimensions;
		image = doc->thumb;
	} break;

	case MediaTypePhoto: {
		_photo = true;
		auto photo = static_cast<HistoryPhoto*>(media)->getPhoto();
		dimensions = QSize(photo->full->width(), photo->full->height());
		image = photo->full;
	} break;

	case MediaTypeVideo: {
		_animated = true;
		doc = static_cast<HistoryVideo*>(media)->getDocument();
		dimensions = doc->dimensions;
		image = doc->thumb;
	} break;

	case MediaTypeGrouped: {
		if (const auto photo = media->getPhoto()) {
			dimensions = QSize(photo->full->width(), photo->full->height());
			image = photo->full;
		} else if (const auto doc = media->getDocument()) {
			dimensions = doc->dimensions;
			image = doc->thumb;
			_animated = true;
		}
	} break;

	case MediaTypeFile:
	case MediaTypeMusicFile:
	case MediaTypeVoiceFile: {
		_doc = true;
		doc = static_cast<HistoryDocument*>(media)->getDocument();
		image = doc->thumb;
	} break;
	}
	caption = media->getCaption().text;

	if (!_animated && (dimensions.isEmpty() || doc || image->isNull())) {
		if (image->isNull()) {
			_thumbw = 0;
		} else {
			int32 tw = image->width(), th = image->height();
			if (tw > th) {
				_thumbw = (tw * st::msgFileThumbSize) / th;
			} else {
				_thumbw = st::msgFileThumbSize;
			}
			auto options = Images::Option::Smooth | Images::Option::RoundedSmall | Images::Option::RoundedTopLeft | Images::Option::RoundedTopRight | Images::Option::RoundedBottomLeft | Images::Option::RoundedBottomRight;
			_thumb = Images::pixmap(image->pix().toImage(), _thumbw * cIntRetinaFactor(), 0, options, st::msgFileThumbSize, st::msgFileThumbSize);
		}

		if (doc) {
			auto nameString = doc->isVoiceMessage()
				? lang(lng_media_audio)
				: doc->composeNameString();
			_name.setText(
				st::semiboldTextStyle,
				nameString,
				_textNameOptions);
			_status = formatSizeText(doc->size);
			_statusw = qMax(
				_name.maxWidth(),
				st::normalFont->width(_status));
			_isImage = doc->isImage();
			_isAudio = (doc->isVoiceMessage() || doc->isAudioFile());
		}
	} else {
		int32 maxW = 0, maxH = 0;
		if (_animated) {
			int32 limitW = st::sendMediaPreviewSize;
			int32 limitH = st::confirmMaxHeight;
			maxW = qMax(dimensions.width(), 1);
			maxH = qMax(dimensions.height(), 1);
			if (maxW * limitH > maxH * limitW) {
				if (maxW < limitW) {
					maxH = maxH * limitW / maxW;
					maxW = limitW;
				}
			} else {
				if (maxH < limitH) {
					maxW = maxW * limitH / maxH;
					maxH = limitH;
				}
			}
			_thumb = image->pixNoCache(maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), Images::Option::Smooth | Images::Option::Blurred, maxW, maxH);
			prepareGifPreview(doc);
		} else {
			maxW = dimensions.width();
			maxH = dimensions.height();
			_thumb = image->pixNoCache(maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), Images::Option::Smooth, maxW, maxH);
		}
		int32 tw = _thumb.width(), th = _thumb.height();
		if (!tw || !th) {
			tw = th = 1;
		}
		_thumbw = st::sendMediaPreviewSize;
		if (_thumb.width() < _thumbw) {
			_thumbw = (_thumb.width() > 20) ? _thumb.width() : 20;
		}
		int32 maxthumbh = qMin(qRound(1.5 * _thumbw), int(st::confirmMaxHeight));
		_thumbh = qRound(th * float64(_thumbw) / tw);
		if (_thumbh > maxthumbh) {
			_thumbw = qRound(_thumbw * float64(maxthumbh) / _thumbh);
			_thumbh = maxthumbh;
			if (_thumbw < 10) {
				_thumbw = 10;
			}
		}
		_thumbx = (st::boxWideWidth - _thumbw) / 2;

		_thumb = App::pixmapFromImageInPlace(_thumb.toImage().scaled(_thumbw * cIntRetinaFactor(), _thumbh * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		_thumb.setDevicePixelRatio(cRetinaFactor());
	}
	Assert(_animated || _photo || _doc);

	_field.create(this, st::confirmCaptionArea, langFactory(lng_photo_caption), caption);
	_field->setMaxLength(MaxPhotoCaption);
	_field->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);
}

void EditCaptionBox::prepareGifPreview(DocumentData *document) {
	auto createGifPreview = [document] {
		return (document && document->isAnimation());
	};
	auto createGifPreviewResult = createGifPreview(); // Clang freeze workaround.
	if (createGifPreviewResult) {
		_gifPreview = Media::Clip::MakeReader(document, _msgId, [this](Media::Clip::Notification notification) {
			clipCallback(notification);
		});
		if (_gifPreview) _gifPreview->setAutoplay();
	}
}

void EditCaptionBox::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gifPreview && _gifPreview->state() == State::Error) {
			_gifPreview.setBad();
		}

		if (_gifPreview && _gifPreview->ready() && !_gifPreview->started()) {
			auto s = QSize(_thumbw, _thumbh);
			_gifPreview->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gifPreview && !_gifPreview->currentDisplayed()) {
			update();
		}
	} break;
	}
}

void EditCaptionBox::prepare() {
	addButton(langFactory(lng_settings_save), [this] { save(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	updateBoxSize();
	connect(_field, &Ui::InputArea::submitted, this, [this] { save(); });
	connect(_field, &Ui::InputArea::cancelled, this, [this] {
		closeBox();
	});
	connect(_field, &Ui::InputArea::resized, this, [this] {
		captionResized();
	});

	auto cursor = _field->textCursor();
	cursor.movePosition(QTextCursor::End);
	_field->setTextCursor(cursor);
}

void EditCaptionBox::captionResized() {
	updateBoxSize();
	resizeEvent(0);
	update();
}

void EditCaptionBox::updateBoxSize() {
	auto newHeight = st::boxPhotoPadding.top() + st::boxPhotoCaptionSkip + _field->height() + errorTopSkip() + st::normalFont->height;
	if (_photo || _animated) {
		newHeight += _thumbh;
	} else if (_thumbw) {
		newHeight += 0 + st::msgFileThumbSize + 0;
	} else if (_doc) {
		newHeight += 0 + st::msgFileSize + 0;
	} else {
		newHeight += st::boxTitleFont->height;
	}
	setDimensions(st::boxWideWidth, newHeight);
}

int EditCaptionBox::errorTopSkip() const {
	return (st::boxButtonPadding.top() / 2);
}

void EditCaptionBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_photo || _animated) {
		if (_thumbx > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _thumbx - st::boxPhotoPadding.left(), _thumbh, st::confirmBg);
		}
		if (_thumbx + _thumbw < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_thumbx + _thumbw, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _thumbx - _thumbw, _thumbh, st::confirmBg);
		}
		if (_gifPreview && _gifPreview->started()) {
			auto s = QSize(_thumbw, _thumbh);
			auto paused = controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
			auto frame = _gifPreview->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : getms());
			p.drawPixmap(_thumbx, st::boxPhotoPadding.top(), frame);
		} else {
			p.drawPixmap(_thumbx, st::boxPhotoPadding.top(), _thumb);
		}
		if (_animated && !_gifPreview) {
			QRect inner(_thumbx + (_thumbw - st::msgFileSize) / 2, st::boxPhotoPadding.top() + (_thumbh - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgDateImgBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			auto icon = &st::historyFileInPlay;
			icon->paintInCenter(p, inner);
		}
	} else if (_doc) {
		int32 w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		int32 h = _thumbw ? (0 + st::msgFileThumbSize + 0) : (0 + st::msgFileSize + 0);
		int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0;
		if (_thumbw) {
			nameleft = 0 + st::msgFileThumbSize + st::msgFileThumbPadding.right();
			nametop = st::msgFileThumbNameTop - st::msgFileThumbPadding.top();
			nameright = 0;
			statustop = st::msgFileThumbStatusTop - st::msgFileThumbPadding.top();
		} else {
			nameleft = 0 + st::msgFileSize + st::msgFilePadding.right();
			nametop = st::msgFileNameTop - st::msgFilePadding.top();
			nameright = 0;
			statustop = st::msgFileStatusTop - st::msgFilePadding.top();
		}
		int32 namewidth = w - nameleft - 0;
		if (namewidth > _statusw) {
			//w -= (namewidth - _statusw);
			//namewidth = _statusw;
		}
		int32 x = (width() - w) / 2, y = st::boxPhotoPadding.top();

//		App::roundRect(p, x, y, w, h, st::msgInBg, MessageInCorners, &st::msgInShadow);

		if (_thumbw) {
			QRect rthumb(rtlrect(x + 0, y + 0, st::msgFileThumbSize, st::msgFileThumbSize, width()));
			p.drawPixmap(rthumb.topLeft(), _thumb);
		} else {
			QRect inner(rtlrect(x + 0, y + 0, st::msgFileSize, st::msgFileSize, width()));
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgFileInBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			auto icon = &(_isAudio ? st::historyFileInPlay : _isImage ? st::historyFileInImage : st::historyFileInDocument);
			icon->paintInCenter(p, inner);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

		auto &status = st::mediaInFg;
		p.setFont(st::normalFont);
		p.setPen(status);
		p.drawTextLeft(x + nameleft, y + statustop, width(), _status);
	} else {
		p.setFont(st::boxTitleFont);
		p.setPen(st::boxTextFg);
		p.drawTextLeft(_field->x(), st::boxPhotoPadding.top(), width(), lang(lng_edit_message));
	}

	if (!_error.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::boxTextFgError);
		p.drawTextLeft(_field->x(), _field->y() + _field->height() + errorTopSkip(), width(), _error);
	}
}

void EditCaptionBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_field->resize(st::sendMediaPreviewSize, _field->height());
	_field->moveToLeft(st::boxPhotoPadding.left(), height() - st::normalFont->height - errorTopSkip() - _field->height());
}

void EditCaptionBox::setInnerFocus() {
	_field->setFocusFast();
}

void EditCaptionBox::save() {
	if (_saveRequestId) return;

	auto item = App::histItemById(_msgId);
	if (!item) {
		_error = lang(lng_edit_deleted);
		update();
		return;
	}

	auto flags = MTPmessages_EditMessage::Flag::f_message | 0;
	if (_previewCancelled) {
		flags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	MTPVector<MTPMessageEntity> sentEntities;
	if (!sentEntities.v.isEmpty()) {
		flags |= MTPmessages_EditMessage::Flag::f_entities;
	}
	auto text = TextUtilities::PrepareForSending(_field->getLastText(), TextUtilities::PrepareTextOption::CheckLinks);
	_saveRequestId = MTP::send(
		MTPmessages_EditMessage(
			MTP_flags(flags),
			item->history()->peer->input,
			MTP_int(item->id),
			MTP_string(text),
			MTPnullMarkup,
			sentEntities,
			MTP_inputGeoPointEmpty()),
		rpcDone(&EditCaptionBox::saveDone),
		rpcFail(&EditCaptionBox::saveFail));
}

void EditCaptionBox::saveDone(const MTPUpdates &updates) {
	_saveRequestId = 0;
	closeBox();
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
}

bool EditCaptionBox::saveFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	QString err = error.type();
	if (err == qstr("MESSAGE_ID_INVALID") || err == qstr("CHAT_ADMIN_REQUIRED") || err == qstr("MESSAGE_EDIT_TIME_EXPIRED")) {
		_error = lang(lng_edit_error);
	} else if (err == qstr("MESSAGE_NOT_MODIFIED")) {
		closeBox();
		return true;
	} else if (err == qstr("MESSAGE_EMPTY")) {
		_field->setFocus();
		_field->showError();
	} else {
		_error = lang(lng_edit_error);
	}
	update();
	return true;
}
