/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/location_picker.h"

#include "base/platform/base_platform_info.h"
#include "core/current_geo_location.h"
#include "lang/lang_keys.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Ui {
namespace {

#ifdef Q_OS_MAC
const auto kProtocolOverride = "mapboxapihelper";
#else // Q_OS_MAC
const auto kProtocolOverride = "";
#endif // Q_OS_MAC

Core::GeoLocation LastExactLocation;
QString MapsProviderToken;

[[nodiscard]] QByteArray DefaultCenter() {
	if (!LastExactLocation) {
		return "null";
	}
	return "["_q
		+ QByteArray::number(LastExactLocation.point.x())
		+ ","_q
		+ QByteArray::number(LastExactLocation.point.y())
		+ "]"_q;
}

[[nodiscard]] QByteArray DefaultBounds() {
	const auto country = Core::ResolveCurrentCountryLocation();
	if (!country) {
		return "null";
	}
	return "[["_q
		+ QByteArray::number(country.bounds.x())
		+ ","_q
		+ QByteArray::number(country.bounds.y())
		+ "],["_q
		+ QByteArray::number(country.bounds.x() + country.bounds.width())
		+ ","_q
		+ QByteArray::number(country.bounds.y() + country.bounds.height())
		+ "]]"_q;
}

[[nodiscard]] QByteArray ComputeStyles() {
	return "";
}

[[nodiscard]] QByteArray EscapeForAttribute(QByteArray value) {
	return value
		.replace('&', "&amp;")
		.replace('"', "&quot;")
		.replace('\'', "&#039;")
		.replace('<', "&lt;")
		.replace('>', "&gt;");
}

[[nodiscard]] QByteArray EscapeForScriptString(QByteArray value) {
	return value
		.replace('\\', "\\\\")
		.replace('"', "\\\"")
		.replace('\'', "\\\'");
}

[[nodiscard]] QByteArray ReadResource(const QString &name) {
	auto file = QFile(u":/picker/"_q + name);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] QByteArray PickerContent() {
	return R"(<!DOCTYPE html>
<html style=")"
+ EscapeForAttribute(ComputeStyles())
+ R"(">
	<head>
		<meta charset="utf-8">
		<meta name="robots" content="noindex, nofollow">
		<meta name="viewport" content="width=device-width, initial-scale=1.0">
		<script src="/location/picker.js"></script>
		<link rel="stylesheet" href="/location/picker.css" />
		<script src='https://api.mapbox.com/mapbox-gl-js/v3.4.0/mapbox-gl.js'></script>
		<link href='https://api.mapbox.com/mapbox-gl-js/v3.4.0/mapbox-gl.css' rel='stylesheet' />
	</head>
	<body>
		<div id="marker"><div id="marker_drop"></div></div>
		<div id="map"></div>
		<script>LocationPicker.notify({ event: 'ready' });</script>
	</body>
</html>
)"_q;
}

} // namespace

LocationPicker::LocationPicker(Descriptor &&descriptor)
: _callback(std::move(descriptor.callback))
, _quit(std::move(descriptor.quit))
, _window(std::make_unique<SeparatePanel>())
, _body((_window->setInnerSize(st::pickLocationWindow)
	, _window->showInner(base::make_unique_q<RpWidget>(_window.get()))
	, _window->inner()))
, _updateStyles([=] {
	const auto str = EscapeForScriptString(ComputeStyles());
	if (_webview) {
		_webview->eval("IV.updateStyles('" + str + "');");
	}
}) {
	std::move(
		descriptor.closeRequests
	) | rpl::start_with_next([=] {
		_window = nullptr;
		delete this;
	}, _lifetime);

	setup(descriptor);
}

bool LocationPicker::Available(const QString &token) {
	static const auto Supported = Webview::NavigateToDataSupported();
	MapsProviderToken = token;
	return Supported && !MapsProviderToken.isEmpty();
}

void LocationPicker::setup(const Descriptor &descriptor) {
	setupWindow(descriptor);
	setupWebview(descriptor);
}

void LocationPicker::setupWindow(const Descriptor &descriptor) {
	const auto window = _window.get();

	window->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	window->closeRequests() | rpl::start_with_next([=] {
		close();
	}, _lifetime);

	const auto parent = descriptor.parent
		? descriptor.parent->window()->geometry()
		: QGuiApplication::primaryScreen()->availableGeometry();
	window->setTitle(tr::lng_maps_point());
	window->move(
		parent.x() + (parent.width() - window->width()) / 2,
		parent.y() + (parent.height() - window->height()) / 2);

	_container = CreateChild<RpWidget>(_body.get());
	const auto scroll = CreateChild<ScrollArea>(_body.get());
	const auto controls = scroll->setOwnedWidget(
		object_ptr<VerticalLayout>(scroll));
	const auto toppad = controls->add(object_ptr<RpWidget>(controls));

	const auto button = controls->add(object_ptr<FlatButton>(
		controls,
		tr::lng_maps_point_send(tr::now),
		st::dialogsUpdateButton));
	button->setClickedCallback([=] {
		_webview->eval("LocationPicker.send();");
	});
	controls->add(object_ptr<RpWidget>(controls))->resize(
		st::pickLocationWindow);

	rpl::combine(
		_body->sizeValue(),
		scroll->scrollTopValue()
	) | rpl::start_with_next([=](QSize size, int scrollTop) {
		const auto width = size.width();
		const auto height = size.height();
		const auto sub = std::min(
			(st::pickLocationMapHeight - st::pickLocationCollapsedHeight),
			scrollTop);
		const auto mapHeight = st::pickLocationMapHeight - sub;
		const auto scrollHeight = height - mapHeight;
		button->resizeToWidth(width);
		_container->setGeometry(0, 0, width, mapHeight);
		scroll->setGeometry(0, mapHeight, width, scrollHeight);
		toppad->resize(width, sub);
	}, _container->lifetime());

	_container->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_container->show();
	scroll->show();
	controls->show();
	button->show();
	window->show();
}

void LocationPicker::setupWebview(const Descriptor &descriptor) {
	Expects(!_webview);

	const auto window = _window.get();
	_webview = std::make_unique<Webview::Window>(
		_container,
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.storageId = descriptor.storageId,
			.dataProtocolOverride = kProtocolOverride,
		});
	const auto raw = _webview.get();

	window->lifetime().add([=] {
		_webview = nullptr;
	});

	window->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			close();
		} else if (e->type() == QEvent::KeyPress) {
			const auto event = static_cast<QKeyEvent*>(e.get());
			if (event->key() == Qt::Key_Escape) {
				close();
			}
		}
	}, window->lifetime());
	raw->widget()->show();

	_container->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		raw->widget()->setGeometry(QRect(QPoint(), size));
	}, _container->lifetime());

	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		return true;
	});
	raw->setNavigationDoneHandler([=](bool success) {
	});
	raw->setMessageHandler([=](const QJsonDocument &message) {
		crl::on_main(_window.get(), [=] {
			const auto object = message.object();
			const auto event = object.value("event").toString();
			if (event == u"ready"_q) {
				initMap();
				resolveCurrentLocation();
			} else if (event == u"keydown"_q) {
				const auto key = object.value("key").toString();
				const auto modifier = object.value("modifier").toString();
				processKey(key, modifier);
			} else if (event == u"send"_q) {
				const auto lat = object.value("latitude").toDouble();
				const auto lon = object.value("longitude").toDouble();
				_callback({ lat, lon });
				close();
			}
		});
	});
	raw->setDataRequestHandler([=](Webview::DataRequest request) {
		const auto pos = request.id.find('#');
		if (pos != request.id.npos) {
			request.id = request.id.substr(0, pos);
		}
		if (!request.id.starts_with("location/")) {
			return Webview::DataResult::Failed;
		}
		const auto finishWith = [&](QByteArray data, std::string mime) {
			request.done({
				.stream = std::make_unique<Webview::DataStreamFromMemory>(
					std::move(data),
					std::move(mime)),
				});
			return Webview::DataResult::Done;
		};
		if (!_subscribedToColors) {
			_subscribedToColors = true;

			rpl::merge(
				Lang::Updated(),
				style::PaletteChanged()
			) | rpl::start_with_next([=] {
				_updateStyles.call();
			}, _webview->lifetime());
		}
		const auto id = std::string_view(request.id).substr(9);
		if (id == "picker.html") {
			return finishWith(PickerContent(), "text/html; charset=utf-8");
		}
		const auto css = id.ends_with(".css");
		const auto js = !css && id.ends_with(".js");
		if (!css && !js) {
			return Webview::DataResult::Failed;
		}
		const auto qstring = QString::fromUtf8(id.data(), id.size());
		const auto pattern = u"^[a-zA-Z\\.\\-_0-9]+$"_q;
		if (QRegularExpression(pattern).match(qstring).hasMatch()) {
			const auto bytes = ReadResource(qstring);
			if (!bytes.isEmpty()) {
				const auto mime = css ? "text/css" : "text/javascript";
				return finishWith(bytes, mime);
			}
		}
		return Webview::DataResult::Failed;
	});

	raw->init(R"()");
	raw->navigateToData("location/picker.html");
}

void LocationPicker::initMap() {
	const auto token = MapsProviderToken.toUtf8();
	const auto center = DefaultCenter();
	const auto bounds = DefaultBounds();
	const auto protocol = *kProtocolOverride
		? "'"_q + kProtocolOverride + "'"
		: "null";
	const auto params = "token: '" + token + "'"
		+ ", center: " + center
		+ ", bounds: " + bounds
		+ ", protocol: " + protocol;
	_webview->eval("LocationPicker.init({ " + params + " });");
}

void LocationPicker::resolveCurrentLocation() {
	using namespace Core;
	const auto window = _window.get();
	ResolveCurrentGeoLocation(crl::guard(window, [=](GeoLocation location) {
		if (location.accuracy != GeoLocationAccuracy::Exact) {
			return;
		}
		LastExactLocation = location;
		if (_webview) {
			const auto point = QByteArray::number(location.point.x())
				+ ","_q
				+ QByteArray::number(location.point.y());
			_webview->eval("LocationPicker.narrowTo([" + point + "]);");
		}
	}));
}

void LocationPicker::processKey(
		const QString &key,
		const QString &modifier) {
	const auto ctrl = ::Platform::IsMac() ? u"cmd"_q : u"ctrl"_q;
	if (key == u"escape"_q || (key == u"w"_q && modifier == ctrl)) {
		close();
	} else if (key == u"m"_q && modifier == ctrl) {
		minimize();
	} else if (key == u"q"_q && modifier == ctrl) {
		quit();
	}
}

void LocationPicker::close() {
	crl::on_main(this, [=] {
		_window = nullptr;
		delete this;
	});
}

void LocationPicker::minimize() {
	if (_window) {
		_window->setWindowState(_window->windowState()
			| Qt::WindowMinimized);
	}
}

void LocationPicker::quit() {
	if (const auto onstack = _quit) {
		onstack();
	}
}

not_null<LocationPicker*> LocationPicker::Show(Descriptor &&descriptor) {
	return new LocationPicker(std::move(descriptor));
}

} // namespace Ui
