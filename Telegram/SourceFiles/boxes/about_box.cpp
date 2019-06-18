/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/about_box.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "platform/platform_file_utilities.h"
#include "platform/platform_info.h"
#include "core/click_handler_types.h"
#include "core/update_checker.h"
#include "styles/style_boxes.h"

namespace {

rpl::producer<TextWithEntities> Text1() {
	return rpl::single(lng_about_text1__rich(
		lt_api_link,
		Ui::Text::Link(
			lang(lng_about_text1_api),
			"https://core.telegram.org/api")));
}

rpl::producer<TextWithEntities> Text2() {
	return rpl::single(lng_about_text2__rich(
		lt_gpl_link,
		Ui::Text::Link(
			lang(lng_about_text2_gpl),
			"https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE"),
		lt_github_link,
		Ui::Text::Link(
			lang(lng_about_text2_github),
			"https://github.com/telegramdesktop/tdesktop")));
}

rpl::producer<TextWithEntities> Text3() {
	return rpl::single(lng_about_text3__rich(
		lt_faq_link,
		Ui::Text::Link(lang(lng_about_text3_faq), telegramFaqLink())));
}

} // namespace

AboutBox::AboutBox(QWidget *parent)
: _version(this, lng_about_version(lt_version, currentVersionText()), st::aboutVersionLink)
, _text1(this, Text1(), st::aboutLabel)
, _text2(this, Text2(), st::aboutLabel)
, _text3(this, Text3(), st::aboutLabel) {
}

void AboutBox::prepare() {
	setTitle(rpl::single(qsl("Telegram Desktop")));

	addButton(langFactory(lng_close), [this] { closeBox(); });

	_text1->setLinksTrusted();
	_text2->setLinksTrusted();
	_text3->setLinksTrusted();

	_version->setClickedCallback([this] { showVersionHistory(); });

	setDimensions(st::aboutWidth, st::aboutTextTop + _text1->height() + st::aboutSkip + _text2->height() + st::aboutSkip + _text3->height());
}

void AboutBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_version->moveToLeft(st::boxPadding.left(), st::aboutVersionTop);
	_text1->moveToLeft(st::boxPadding.left(), st::aboutTextTop);
	_text2->moveToLeft(st::boxPadding.left(), _text1->y() + _text1->height() + st::aboutSkip);
	_text3->moveToLeft(st::boxPadding.left(), _text2->y() + _text2->height() + st::aboutSkip);
}

void AboutBox::showVersionHistory() {
	if (cRealAlphaVersion()) {
		auto url = qsl("https://tdesktop.com/");
		if (Platform::IsWindows()) {
			url += qsl("win/%1.zip");
		} else if (Platform::IsMacOldBuild()) {
			url += qsl("mac32/%1.zip");
		} else if (Platform::IsMac()) {
			url += qsl("mac/%1.zip");
		} else if (Platform::IsLinux32Bit()) {
			url += qsl("linux32/%1.tar.xz");
		} else if (Platform::IsLinux64Bit()) {
			url += qsl("linux/%1.tar.xz");
		} else {
			Unexpected("Platform value.");
		}
		url = url.arg(qsl("talpha%1_%2").arg(cRealAlphaVersion()).arg(Core::countAlphaVersionSignature(cRealAlphaVersion())));

		QApplication::clipboard()->setText(url);

		Ui::show(Box<InformBox>("The link to the current private alpha version of Telegram Desktop was copied to the clipboard."));
	} else {
		QDesktopServices::openUrl(qsl("https://desktop.telegram.org/changelog"));
	}
}

void AboutBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		closeBox();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

QString telegramFaqLink() {
	const auto result = qsl("https://telegram.org/faq");
	const auto langpacked = [&](const char *language) {
		return result + '/' + language;
	};
	const auto current = Lang::Current().id();
	for (const auto language : { "de", "es", "it", "ko" }) {
		if (current.startsWith(QLatin1String(language))) {
			return langpacked(language);
		}
	}
	if (current.startsWith(qstr("pt-br"))) {
		return langpacked("br");
	}
	return result;
}

QString currentVersionText() {
	auto result = QString::fromLatin1(AppVersionStr);
	if (cAlphaVersion()) {
		result += qsl(" alpha %1").arg(cAlphaVersion() % 1000);
	} else if (AppBetaVersion) {
		result += " beta";
	}
	return result;
}
