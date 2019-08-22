/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_chat.h"

#include "settings/settings_common.h"
#include "boxes/connection_box.h"
#include "boxes/auto_download_box.h"
#include "boxes/stickers_box.h"
#include "boxes/background_box.h"
#include "boxes/background_preview_box.h"
#include "boxes/download_path_box.h"
#include "boxes/local_storage_box.h"
#include "boxes/edit_color_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/effects/radial_animation.h"
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "lang/lang_keys.h"
#include "window/themes/window_theme_editor.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "info/profile/info_profile_button.h"
#include "storage/localstorage.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "chat_helpers/emoji_sets_manager.h"
#include "platform/platform_info.h"
#include "support/support_common.h"
#include "support/support_templates.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

namespace Settings {
namespace {

const auto kColorizeIgnoredKeys = base::flat_set<QLatin1String>{ {
	qstr("boxTextFgGood"),
	qstr("boxTextFgError"),
	qstr("historyPeer1NameFg"),
	qstr("historyPeer1NameFgSelected"),
	qstr("historyPeer1UserpicBg"),
	qstr("historyPeer2NameFg"),
	qstr("historyPeer2NameFgSelected"),
	qstr("historyPeer2UserpicBg"),
	qstr("historyPeer3NameFg"),
	qstr("historyPeer3NameFgSelected"),
	qstr("historyPeer3UserpicBg"),
	qstr("historyPeer4NameFg"),
	qstr("historyPeer4NameFgSelected"),
	qstr("historyPeer4UserpicBg"),
	qstr("historyPeer5NameFg"),
	qstr("historyPeer5NameFgSelected"),
	qstr("historyPeer5UserpicBg"),
	qstr("historyPeer6NameFg"),
	qstr("historyPeer6NameFgSelected"),
	qstr("historyPeer6UserpicBg"),
	qstr("historyPeer7NameFg"),
	qstr("historyPeer7NameFgSelected"),
	qstr("historyPeer7UserpicBg"),
	qstr("historyPeer8NameFg"),
	qstr("historyPeer8NameFgSelected"),
	qstr("historyPeer8UserpicBg"),
	qstr("msgFile1Bg"),
	qstr("msgFile1BgDark"),
	qstr("msgFile1BgOver"),
	qstr("msgFile1BgSelected"),
	qstr("msgFile2Bg"),
	qstr("msgFile2BgDark"),
	qstr("msgFile2BgOver"),
	qstr("msgFile2BgSelected"),
	qstr("msgFile3Bg"),
	qstr("msgFile3BgDark"),
	qstr("msgFile3BgOver"),
	qstr("msgFile3BgSelected"),
	qstr("msgFile4Bg"),
	qstr("msgFile4BgDark"),
	qstr("msgFile4BgOver"),
	qstr("msgFile4BgSelected"),
	qstr("mediaviewFileRedCornerFg"),
	qstr("mediaviewFileYellowCornerFg"),
	qstr("mediaviewFileGreenCornerFg"),
	qstr("mediaviewFileBlueCornerFg"),
} };

} // namespace

class BackgroundRow : public Ui::RpWidget {
public:
	BackgroundRow(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	void updateImage();

	float64 radialProgress() const;
	bool radialLoading() const;
	QRect radialRect() const;
	void radialStart();
	crl::time radialTimeShift() const;
	void radialAnimationCallback(crl::time now);

	QPixmap _background;
	object_ptr<Ui::LinkButton> _chooseFromGallery;
	object_ptr<Ui::LinkButton> _chooseFromFile;

	Ui::RadialAnimation _radial;

};

class DefaultTheme final : public Ui::AbstractCheckView {
public:
	enum class Type {
		DayBlue,
		Default,
		Night,
		NightGreen,
	};
	struct Scheme {
		Type type = Type();
		QColor background;
		QColor sent;
		QColor received;
		QColor radiobuttonInactive;
		QColor radiobuttonActive;
		tr::phrase<> name;
		QString path;
		QColor accentColor;
	};
	DefaultTheme(Scheme scheme, bool checked);

	QSize getSize() const override;
	void paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

private:
	void checkedChangedHook(anim::type animated) override;

	Scheme _scheme;
	Ui::RadioView _radio;

};

void ChooseFromFile(
	not_null<::Main::Session*> session,
	not_null<QWidget*> parent);

BackgroundRow::BackgroundRow(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _chooseFromGallery(
	this,
	tr::lng_settings_bg_from_gallery(tr::now),
	st::settingsLink)
, _chooseFromFile(this, tr::lng_settings_bg_from_file(tr::now), st::settingsLink)
, _radial([=](crl::time now) { radialAnimationCallback(now); }) {
	updateImage();

	_chooseFromGallery->addClickHandler([=] {
		Ui::show(Box<BackgroundBox>(&controller->session()));
	});
	_chooseFromFile->addClickHandler([=] {
		ChooseFromFile(&controller->session(), this);
	});

	using Update = const Window::Theme::BackgroundUpdate;
	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::New
			|| update.type == Update::Type::Start
			|| update.type == Update::Type::Changed);
	}) | rpl::start_with_next([=] {
		updateImage();
	}, lifetime());
}

void BackgroundRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto radial = _radial.animating();
	const auto radialOpacity = radial ? _radial.opacity() : 0.;
	if (radial) {
		const auto backThumb = App::main()->newBackgroundThumb();
		if (!backThumb) {
			p.drawPixmap(0, 0, _background);
		} else {
			const auto &pix = backThumb->pixBlurred(
				Data::FileOrigin(),
				st::settingsBackgroundThumb);
			const auto factor = cIntRetinaFactor();
			p.drawPixmap(
				0,
				0,
				st::settingsBackgroundThumb,
				st::settingsBackgroundThumb,
				pix,
				0,
				(pix.height() - st::settingsBackgroundThumb * factor) / 2,
				st::settingsBackgroundThumb * factor,
				st::settingsBackgroundThumb * factor);
		}

		const auto outer = radialRect();
		const auto inner = QRect(
			QPoint(
				outer.x() + (outer.width() - st::radialSize.width()) / 2,
				outer.y() + (outer.height() - st::radialSize.height()) / 2),
			st::radialSize);
		p.setPen(Qt::NoPen);
		p.setOpacity(radialOpacity);
		p.setBrush(st::radialBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(1);
		const auto arc = inner.marginsRemoved(QMargins(
			st::radialLine,
			st::radialLine,
			st::radialLine,
			st::radialLine));
		_radial.draw(p, arc, st::radialLine, st::radialFg);
	} else {
		p.drawPixmap(0, 0, _background);
	}
}

int BackgroundRow::resizeGetHeight(int newWidth) {
	auto linkTop = st::settingsFromGalleryTop;
	auto linkLeft = st::settingsBackgroundThumb + st::settingsThumbSkip;
	auto linkWidth = newWidth - linkLeft;
	_chooseFromGallery->resizeToWidth(
		qMin(linkWidth, _chooseFromGallery->naturalWidth()));
	_chooseFromFile->resizeToWidth(
		qMin(linkWidth, _chooseFromFile->naturalWidth()));
	_chooseFromGallery->moveToLeft(linkLeft, linkTop, newWidth);
	linkTop += _chooseFromGallery->height() + st::settingsFromFileTop;
	_chooseFromFile->moveToLeft(linkLeft, linkTop, newWidth);
	return st::settingsBackgroundThumb;
}

float64 BackgroundRow::radialProgress() const {
	return App::main()->chatBackgroundProgress();
}

bool BackgroundRow::radialLoading() const {
	const auto main = App::main();
	if (main->chatBackgroundLoading()) {
		main->checkChatBackground();
		if (main->chatBackgroundLoading()) {
			return true;
		} else {
			const_cast<BackgroundRow*>(this)->updateImage();
		}
	}
	return false;
}

QRect BackgroundRow::radialRect() const {
	return QRect(
		0,
		0,
		st::settingsBackgroundThumb,
		st::settingsBackgroundThumb);
}

void BackgroundRow::radialStart() {
	if (radialLoading() && !_radial.animating()) {
		_radial.start(radialProgress());
		if (const auto shift = radialTimeShift()) {
			_radial.update(
				radialProgress(),
				!radialLoading(),
				crl::now() + shift);
		}
	}
}

crl::time BackgroundRow::radialTimeShift() const {
	return st::radialDuration;
}

void BackgroundRow::radialAnimationCallback(crl::time now) {
	const auto updated = _radial.update(
		radialProgress(),
		!radialLoading(),
		now + radialTimeShift());
	if (!anim::Disabled() || updated) {
		rtlupdate(radialRect());
	}
}

void BackgroundRow::updateImage() {
	int32 size = st::settingsBackgroundThumb * cIntRetinaFactor();
	QImage back(size, size, QImage::Format_ARGB32_Premultiplied);
	back.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&back);
		PainterHighQualityEnabler hq(p);

		if (const auto color = Window::Theme::Background()->colorForFill()) {
			p.fillRect(
				0,
				0,
				st::settingsBackgroundThumb,
				st::settingsBackgroundThumb,
				*color);
		} else {
			const auto &pix = Window::Theme::Background()->pixmap();
			const auto sx = (pix.width() > pix.height())
				? ((pix.width() - pix.height()) / 2)
				: 0;
			const auto sy = (pix.height() > pix.width())
				? ((pix.height() - pix.width()) / 2)
				: 0;
			const auto s = (pix.width() > pix.height())
				? pix.height()
				: pix.width();
			p.drawPixmap(
				0,
				0,
				st::settingsBackgroundThumb,
				st::settingsBackgroundThumb,
				pix,
				sx,
				sy,
				s,
				s);
		}
	}
	Images::prepareRound(back, ImageRoundRadius::Small);
	_background = App::pixmapFromImageInPlace(std::move(back));
	_background.setDevicePixelRatio(cRetinaFactor());

	rtlupdate(radialRect());

	if (radialLoading()) {
		radialStart();
	}
}

DefaultTheme::DefaultTheme(Scheme scheme, bool checked)
: AbstractCheckView(st::defaultRadio.duration, checked, nullptr)
, _scheme(scheme)
, _radio(st::defaultRadio, checked, [=] { update(); }) {
	_radio.setToggledOverride(_scheme.radiobuttonActive);
	_radio.setUntoggledOverride(_scheme.radiobuttonInactive);
}

QSize DefaultTheme::getSize() const {
	return st::settingsThemePreviewSize;
}

void DefaultTheme::paint(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	const auto received = QRect(
		st::settingsThemeBubblePosition,
		st::settingsThemeBubbleSize);
	const auto sent = QRect(
		outerWidth - received.width() - st::settingsThemeBubblePosition.x(),
		received.y() + received.height() + st::settingsThemeBubbleSkip,
		received.width(),
		received.height());
	const auto radius = st::settingsThemeBubbleRadius;

	p.fillRect(
		QRect(QPoint(), st::settingsThemePreviewSize),
		_scheme.background);

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_scheme.received);
	p.drawRoundedRect(rtlrect(received, outerWidth), radius, radius);
	p.setBrush(_scheme.sent);
	p.drawRoundedRect(rtlrect(sent, outerWidth), radius, radius);

	const auto radio = _radio.getSize();
	_radio.paint(
		p,
		(outerWidth - radio.width()) / 2,
		getSize().height() - radio.height() - st::settingsThemeRadioBottom,
		outerWidth);
}

QImage DefaultTheme::prepareRippleMask() const {
	return QImage();
}

bool DefaultTheme::checkRippleStartPosition(QPoint position) const {
	return false;
}

void DefaultTheme::checkedChangedHook(anim::type animated) {
	_radio.setChecked(checked(), animated);
}

void ChooseFromFile(
		not_null<::Main::Session*> session,
		not_null<QWidget*> parent) {
	const auto &imgExtensions = cImgExtensions();
	auto filters = QStringList(
		qsl("Theme files (*.tdesktop-theme *.tdesktop-palette *")
		+ imgExtensions.join(qsl(" *"))
		+ qsl(")"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = crl::guard(session, [=](
			const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.paths.isEmpty()) {
			const auto filePath = result.paths.front();
			const auto hasExtension = [&](QLatin1String extension) {
				return filePath.endsWith(extension, Qt::CaseInsensitive);
			};
			if (hasExtension(qstr(".tdesktop-theme"))
				|| hasExtension(qstr(".tdesktop-palette"))) {
				Window::Theme::Apply(filePath);
				return;
			}
		}

		auto image = result.remoteContent.isEmpty()
			? App::readImage(result.paths.front())
			: App::readImage(result.remoteContent);
		if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
			return;
		}
		auto local = Data::CustomWallPaper();
		local.setLocalImageAsThumbnail(std::make_shared<Image>(
			std::make_unique<Images::ImageSource>(
				std::move(image),
				"JPG")));
		Ui::show(Box<BackgroundPreviewBox>(session, local));
	});
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		filters.join(qsl(";;")),
		crl::guard(parent, callback));

}

QString DownloadPathText() {
	if (Global::DownloadPath().isEmpty()) {
		return tr::lng_download_path_default(tr::now);
	} else if (Global::DownloadPath() == qsl("tmp")) {
		return tr::lng_download_path_temp(tr::now);
	}
	return QDir::toNativeSeparators(Global::DownloadPath());
}

void SetupStickersEmoji(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_settings_stickers_emoji());

	const auto session = &controller->session();

	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(object_ptr<Ui::OverrideMargins>(
		container,
		std::move(wrap),
		QMargins(0, 0, 0, st::settingsCheckbox.margin.bottom())));

	const auto checkbox = [&](const QString &label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			label,
			checked,
			st::settingsCheckbox);
	};
	const auto add = [&](const QString &label, bool checked, auto &&handle) {
		inner->add(
			checkbox(label, checked),
			st::settingsCheckboxPadding
		)->checkedChanges(
		) | rpl::start_with_next(
			std::move(handle),
			inner->lifetime());
	};

	add(
		tr::lng_settings_large_emoji(tr::now),
		session->settings().largeEmoji(),
		[=](bool checked) {
			session->settings().setLargeEmoji(checked);
			session->saveSettingsDelayed();
		});

	add(
		tr::lng_settings_replace_emojis(tr::now),
		session->settings().replaceEmoji(),
		[=](bool checked) {
			session->settings().setReplaceEmoji(checked);
			session->saveSettingsDelayed();
		});

	add(
		tr::lng_settings_suggest_emoji(tr::now),
		session->settings().suggestEmoji(),
		[=](bool checked) {
			session->settings().setSuggestEmoji(checked);
			session->saveSettingsDelayed();
		});

	add(
		tr::lng_settings_suggest_by_emoji(tr::now),
		session->settings().suggestStickersByEmoji(),
		[=](bool checked) {
			session->settings().setSuggestStickersByEmoji(checked);
			session->saveSettingsDelayed();
		});

	add(
		tr::lng_settings_loop_stickers(tr::now),
		session->settings().loopAnimatedStickers(),
		[=](bool checked) {
			session->settings().setLoopAnimatedStickers(checked);
			session->saveSettingsDelayed();
		});

	AddButton(
		container,
		tr::lng_stickers_you_have(),
		st::settingsChatButton,
		&st::settingsIconStickers,
		st::settingsChatIconLeft
	)->addClickHandler([=] {
		Ui::show(Box<StickersBox>(session, StickersBox::Section::Installed));
	});

	AddButton(
		container,
		tr::lng_emoji_manage_sets(),
		st::settingsChatButton,
		&st::settingsIconEmoji,
		st::settingsChatIconLeft
	)->addClickHandler([] {
		Ui::show(Box<Ui::Emoji::ManageSetsBox>());
	});

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupMessages(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_settings_messages());

	AddSkip(container, st::settingsSendTypeSkip);

	using SendByType = Ui::InputSubmitSettings;

	const auto skip = st::settingsSendTypeSkip;
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skip, 0, skip)));

	const auto group = std::make_shared<Ui::RadioenumGroup<SendByType>>(
		controller->session().settings().sendSubmitWay());
	const auto add = [&](SendByType value, const QString &text) {
		inner->add(
			object_ptr<Ui::Radioenum<SendByType>>(
				inner,
				group,
				value,
				text,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	const auto small = st::settingsSendTypePadding;
	const auto top = skip;
	add(SendByType::Enter, tr::lng_settings_send_enter(tr::now));
	add(
		SendByType::CtrlEnter,
		(Platform::IsMac()
			? tr::lng_settings_send_cmdenter(tr::now)
			: tr::lng_settings_send_ctrlenter(tr::now)));

	group->setChangedCallback([=](SendByType value) {
		controller->session().settings().setSendSubmitWay(value);
		if (App::main()) {
			App::main()->ctrlEnterSubmitUpdated();
		}
		Local::writeUserSettings();
	});

	AddSkip(inner, st::settingsCheckboxesSkip);
}

void SetupExport(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		tr::lng_settings_export_data(),
		st::settingsButton
	)->addClickHandler([=] {
		const auto session = &controller->session();
		Ui::hideSettingsAndLayer();
		App::CallDelayed(
			st::boxDuration,
			session,
			[=] { session->data().startExport(); });
	});
}

void SetupLocalStorage(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddButton(
		container,
		tr::lng_settings_manage_local_storage(),
		st::settingsButton
	)->addClickHandler([=] {
		LocalStorageBox::Show(&controller->session());
	});
}

void SetupDataStorage(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_settings_data_storage());

	const auto ask = AddButton(
		container,
		tr::lng_download_path_ask(),
		st::settingsButton
	)->toggleOn(rpl::single(Global::AskDownloadPath()));

#ifndef OS_WIN_STORE
	const auto showpath = Ui::CreateChild<rpl::event_stream<bool>>(ask);
	const auto path = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				tr::lng_download_path(),
				st::settingsButton)));
	auto pathtext = rpl::single(
		rpl::empty_value()
	) | rpl::then(base::ObservableViewer(
		Global::RefDownloadPathChanged()
	)) | rpl::map([] {
		return DownloadPathText();
	});
	CreateRightLabel(
		path->entity(),
		std::move(pathtext),
		st::settingsButton,
		tr::lng_download_path());
	path->entity()->addClickHandler([] {
		Ui::show(Box<DownloadPathBox>());
	});
	path->toggleOn(ask->toggledValue() | rpl::map(!_1));
#endif // OS_WIN_STORE

	ask->toggledValue(
	) | rpl::filter([](bool checked) {
		return (checked != Global::AskDownloadPath());
	}) | rpl::start_with_next([=](bool checked) {
		Global::SetAskDownloadPath(checked);
		Local::writeUserSettings();

#ifndef OS_WIN_STORE
		showpath->fire_copy(!checked);
#endif // OS_WIN_STORE

	}, ask->lifetime());

	SetupLocalStorage(controller, container);
	SetupExport(controller, container);

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupAutoDownload(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_media_auto_settings());

	using Source = Data::AutoDownload::Source;
	const auto add = [&](rpl::producer<QString> label, Source source) {
		AddButton(
			container,
			std::move(label),
			st::settingsButton
		)->addClickHandler([=] {
			Ui::show(Box<AutoDownloadBox>(&controller->session(), source));
		});
	};
	add(tr::lng_media_auto_in_private(), Source::User);
	add(tr::lng_media_auto_in_groups(), Source::Group);
	add(tr::lng_media_auto_in_channels(), Source::Channel);

	AddSkip(container, st::settingsCheckboxesSkip);
}

void SetupChatBackground(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_settings_section_background());

	container->add(
		object_ptr<BackgroundRow>(container, controller),
		st::settingsBackgroundPadding);

	const auto skipTop = st::settingsCheckbox.margin.top();
	const auto skipBottom = st::settingsCheckbox.margin.bottom();
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skipTop, 0, skipBottom)));

	AddSkip(container, st::settingsTileSkip);

	const auto tile = inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			tr::lng_settings_bg_tile(tr::now),
			Window::Theme::Background()->tile(),
			st::settingsCheckbox),
		st::settingsSendTypePadding);
	const auto adaptive = inner->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			inner,
			object_ptr<Ui::Checkbox>(
				inner,
				tr::lng_settings_adaptive_wide(tr::now),
				Global::AdaptiveForWide(),
				st::settingsCheckbox),
			st::settingsSendTypePadding));

	tile->checkedChanges(
	) | rpl::start_with_next([](bool checked) {
		Window::Theme::Background()->setTile(checked);
	}, tile->lifetime());

	using Update = const Window::Theme::BackgroundUpdate;
	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::Changed);
	}) | rpl::map([] {
		return Window::Theme::Background()->tile();
	}) | rpl::start_with_next([=](bool tiled) {
		tile->setChecked(tiled);
	}, tile->lifetime());

	adaptive->toggleOn(rpl::single(
		rpl::empty_value()
	) | rpl::then(base::ObservableViewer(
		Adaptive::Changed()
	)) | rpl::map([] {
		return (Global::AdaptiveChatLayout() == Adaptive::ChatLayout::Wide);
	}));

	adaptive->entity()->checkedChanges(
	) | rpl::start_with_next([](bool checked) {
		Global::SetAdaptiveForWide(checked);
		Adaptive::Changed().notify();
		Local::writeUserSettings();
	}, adaptive->lifetime());
}

void SetupDefaultThemes(not_null<Ui::VerticalLayout*> container) {
	using Type = DefaultTheme::Type;
	using Scheme = DefaultTheme::Scheme;
	const auto block = container->add(object_ptr<Ui::FixedHeightWidget>(
		container));
	const auto scheme = DefaultTheme::Scheme();
	const auto color = [](str_const hex) {
		Expects(hex.size() == 6);

		const auto component = [](char a, char b) {
			const auto convert = [](char ch) {
				Expects((ch >= '0' && ch <= '9')
					|| (ch >= 'A' && ch <= 'F')
					|| (ch >= 'a' && ch <= 'f'));

				return (ch >= '0' && ch <= '9')
					? int(ch - '0')
					: int(ch - ((ch >= 'A' && ch <= 'F') ? 'A' : 'a') + 10);
			};
			return convert(a) * 16 + convert(b);
		};

		return QColor(
			component(hex[0], hex[1]),
			component(hex[2], hex[3]),
			component(hex[4], hex[5]));
	};
	static const auto schemes = {
		Scheme{
			Type::DayBlue,
			color("7ec4ea"),
			color("d7f0ff"),
			color("ffffff"),
			color("d7f0ff"),
			color("ffffff"),
			tr::lng_settings_theme_blue,
			":/gui/day-blue.tdesktop-theme",
			color("40a7e3")
		},
		Scheme{
			Type::Default,
			color("90ce89"),
			color("eaffdc"),
			color("ffffff"),
			color("eaffdc"),
			color("ffffff"),
			tr::lng_settings_theme_classic,
			QString()
		},
		Scheme{
			Type::Night,
			color("485761"),
			color("5ca7d4"),
			color("6b808d"),
			color("6b808d"),
			color("5ca7d4"),
			tr::lng_settings_theme_midnight,
			":/gui/night.tdesktop-theme",
			color("5288c1")
		},
		Scheme{
			Type::NightGreen,
			color("485761"),
			color("74bf93"),
			color("6b808d"),
			color("6b808d"),
			color("74bf93"),
			tr::lng_settings_theme_matrix,
			":/gui/night-green.tdesktop-theme",
			color("3fc1b0")
		},
	};
	const auto chosen = [&] {
		if (Window::Theme::IsNonDefaultBackground()) {
			return Type(-1);
		}
		const auto path = Window::Theme::Background()->themeAbsolutePath();
		for (const auto &scheme : schemes) {
			if (path == scheme.path) {
				return scheme.type;
			}
		}
		return Type(-1);
	};
	const auto group = std::make_shared<Ui::RadioenumGroup<Type>>(chosen());

	const auto apply = [=](
			const Scheme &scheme,
			const Window::Theme::Colorizer *colorizer = nullptr) {
		const auto isNight = [](const Scheme &scheme) {
			const auto type = scheme.type;
			return (type != Type::DayBlue) && (type != Type::Default);
		};
		const auto currentlyIsCustom = (chosen() == Type(-1));
		if (Window::Theme::IsNightMode() == isNight(scheme)) {
			Window::Theme::ApplyDefaultWithPath(scheme.path, colorizer);
		} else {
			Window::Theme::ToggleNightMode(scheme.path, colorizer);
		}
		if (!currentlyIsCustom) {
			Window::Theme::KeepApplied();
		}
	};
	const auto applyWithColorize = [=](const Scheme &scheme) {
		const auto box = Ui::show(Box<EditColorBox>(
			"Choose accent color",
			scheme.accentColor));
		box->setSaveCallback([=](QColor result) {
			auto colorizer = Window::Theme::Colorizer();
			colorizer.ignoreKeys = kColorizeIgnoredKeys;
			colorizer.hueThreshold = 10;
			scheme.accentColor.getHsv(
				&colorizer.wasHue,
				&colorizer.wasSaturation,
				&colorizer.wasValue);
			result.getHsv(
				&colorizer.nowHue,
				&colorizer.nowSaturation,
				&colorizer.nowValue);
			apply(scheme, &colorizer);
		});
	};
	const auto schemeClicked = [=](
			const Scheme &scheme,
			Qt::KeyboardModifiers modifiers) {
		if (scheme.accentColor.hue() && (modifiers & Qt::ControlModifier)) {
			applyWithColorize(scheme);
		} else {
			apply(scheme);
		}
	};

	auto buttons = ranges::view::all(
		schemes
	) | ranges::view::transform([&](const Scheme &scheme) {
		auto check = std::make_unique<DefaultTheme>(scheme, false);
		const auto weak = check.get();
		const auto result = Ui::CreateChild<Ui::Radioenum<Type>>(
			block,
			group,
			scheme.type,
			scheme.name(tr::now),
			st::settingsTheme,
			std::move(check));
		result->addClickHandler([=] {
			schemeClicked(scheme, result->clickModifiers());
		});
		weak->setUpdateCallback([=] { result->update(); });
		return result;
	}) | ranges::to_vector;

	using Update = const Window::Theme::BackgroundUpdate;
	base::ObservableViewer(
		*Window::Theme::Background()
	) | rpl::filter([](const Update &update) {
		return (update.type == Update::Type::ApplyingTheme
			|| update.type == Update::Type::New);
	}) | rpl::map([=] {
		return chosen();
	}) | rpl::start_with_next([=](Type type) {
		group->setValue(type);
	}, container->lifetime());

	for (const auto button : buttons) {
		button->setCheckAlignment(style::al_top);
		button->resizeToWidth(button->width());
	}
	block->resize(block->width(), buttons[0]->height());
	block->widthValue(
	) | rpl::start_with_next([buttons = std::move(buttons)](int width) {
		Expects(!buttons.empty());

		//     |------|      |---------|        |-------|      |-------|
		// pad | blue | skip | classic | 3*skip | night | skip | night | pad
		//     |------|      |---------|        |-------|      |-------|
		const auto padding = st::settingsButton.padding;
		width -= padding.left() + padding.right();
		const auto desired = st::settingsThemePreviewSize.width();
		const auto count = int(buttons.size());
		const auto smallSkips = (count / 2);
		const auto bigSkips = ((count - 1) / 2);
		const auto skipRatio = 3;
		const auto skipSegments = smallSkips + bigSkips * skipRatio;
		const auto minSkip = st::settingsThemeMinSkip;
		const auto single = [&] {
			if (width >= skipSegments * minSkip + count * desired) {
				return desired;
			}
			return (width - skipSegments * minSkip) / count;
		}();
		if (single <= 0) {
			return;
		}
		const auto fullSkips = width - count * single;
		const auto segment = fullSkips / float64(skipSegments);
		const auto smallSkip = segment;
		const auto bigSkip = segment * skipRatio;
		auto left = padding.left() + 0.;
		auto index = 0;
		for (const auto button : buttons) {
			button->resizeToWidth(single);
			button->moveToLeft(int(std::round(left)), 0);
			left += button->width() + ((index++ % 2) ? bigSkip : smallSkip);
		}
	}, block->lifetime());

	AddSkip(container);
}

void SetupThemeOptions(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsPrivacySkip);

	AddSubsectionTitle(container, tr::lng_settings_themes());

	AddSkip(container, st::settingsThemesTopSkip);
	SetupDefaultThemes(container);
	AddSkip(container, st::settingsThemesBottomSkip);

	AddButton(
		container,
		tr::lng_settings_bg_edit_theme(),
		st::settingsChatButton,
		&st::settingsIconThemes,
		st::settingsChatIconLeft
	)->addClickHandler(App::LambdaDelayed(
		st::settingsChatButton.ripple.hideDuration,
		container,
		[] { Window::Theme::Editor::Start(); }));

	AddSkip(container);
}

void SetupSupportSwitchSettings(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using SwitchType = Support::SwitchSettings;
	const auto group = std::make_shared<Ui::RadioenumGroup<SwitchType>>(
		controller->session().settings().supportSwitch());
	const auto add = [&](SwitchType value, const QString &label) {
		container->add(
			object_ptr<Ui::Radioenum<SwitchType>>(
				container,
				group,
				value,
				label,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	add(SwitchType::None, "Just send the reply");
	add(SwitchType::Next, "Send and switch to next");
	add(SwitchType::Previous, "Send and switch to previous");
	group->setChangedCallback([=](SwitchType value) {
		controller->session().settings().setSupportSwitch(value);
		Local::writeUserSettings();
	});
}

void SetupSupportChatsLimitSlice(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	constexpr auto kDayDuration = 24 * 60 * 60;
	struct Option {
		int days = 0;
		QString label;
	};
	const auto options = std::vector<Option>{
		{ 1, "1 day" },
		{ 7, "1 week" },
		{ 30, "1 month" },
		{ 365, "1 year" },
		{ 0, "All of them" },
	};
	const auto current = controller->session().settings().supportChatsTimeSlice();
	const auto days = current / kDayDuration;
	const auto best = ranges::min_element(
		options,
		std::less<>(),
		[&](const Option &option) { return std::abs(option.days - days); });

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(best->days);
	for (const auto &option : options) {
		container->add(
			object_ptr<Ui::Radiobutton>(
				container,
				group,
				option.days,
				option.label,
				st::settingsSendType),
			st::settingsSendTypePadding);
	}
	group->setChangedCallback([=](int days) {
		controller->session().settings().setSupportChatsTimeSlice(
			days * kDayDuration);
		Local::writeUserSettings();
	});
}

void SetupSupport(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);

	AddSubsectionTitle(container, rpl::single(qsl("Support settings")));

	AddSkip(container, st::settingsSendTypeSkip);

	const auto skip = st::settingsSendTypeSkip;
	auto wrap = object_ptr<Ui::VerticalLayout>(container);
	const auto inner = wrap.data();
	container->add(
		object_ptr<Ui::OverrideMargins>(
			container,
			std::move(wrap),
			QMargins(0, skip, 0, skip)));

	SetupSupportSwitchSettings(controller, inner);

	AddSkip(inner, st::settingsCheckboxesSkip);

	inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			"Enable templates autocomplete",
			controller->session().settings().supportTemplatesAutocomplete(),
			st::settingsCheckbox),
		st::settingsSendTypePadding
	)->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		controller->session().settings().setSupportTemplatesAutocomplete(
			checked);
		Local::writeUserSettings();
	}, inner->lifetime());

	AddSkip(inner, st::settingsCheckboxesSkip);

	AddSubsectionTitle(inner, rpl::single(qsl("Load chats for a period")));

	SetupSupportChatsLimitSlice(controller, inner);

	AddSkip(inner, st::settingsCheckboxesSkip);

	AddSkip(inner);
}

Chat::Chat(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

void Chat::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupThemeOptions(content);
	SetupChatBackground(controller, content);
	SetupStickersEmoji(controller, content);
	SetupMessages(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
