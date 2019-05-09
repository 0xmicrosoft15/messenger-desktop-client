/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_animation.h"

#include "lottie/lottie_frame_renderer.h"
#include "base/algorithm.h"

#include <range/v3/view/reverse.hpp>
#include <QtMath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QFile>
#include <QPointF>
#include <QPainter>
#include <QImage>
#include <QTimer>
#include <QMetaObject>
#include <QLoggingCategory>
#include <QThread>
#include <math.h>

#include <QtBodymovin/private/bmbase_p.h>
#include <QtBodymovin/private/bmlayer_p.h>
#include <QtBodymovin/private/bmasset_p.h>

#include "rasterrenderer/lottierasterrenderer.h"

namespace Lottie {

bool ValidateFile(const QString &path) {
	if (!path.endsWith(qstr(".json"), Qt::CaseInsensitive)) {
		return false;
	}
	return true;
}

std::unique_ptr<Animation> FromFile(const QString &path) {
	if (!path.endsWith(qstr(".json"), Qt::CaseInsensitive)) {
		return nullptr;
	}
	auto f = QFile(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return nullptr;
	}
	const auto content = f.readAll();
	if (content.isEmpty()) {
		return nullptr;
	}
	return std::make_unique<Lottie::Animation>(content);
}

Animation::Animation(const QByteArray &content) {
	parse(content);
}

Animation::~Animation() {
}

QImage Animation::frame(crl::time now) const {
	if (_startFrame == _endFrame || _realWidth <= 0 || _realHeight <= 0) {
		return QImage();
	}
	auto result = QImage(
		qCeil(_realWidth),
		qCeil(_realHeight),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	{
		QPainter p(&result);
		p.setRenderHints(QPainter::Antialiasing);
		p.setRenderHints(QPainter::SmoothPixmapTransform);

		const auto position = now;
		const auto elapsed = int((_frameRate * position + 500) / 1000);
		const auto frames = (_endFrame - _startFrame);
		const auto frame = _options.loop
			? (_startFrame + (elapsed % frames))
			: std::min(_startFrame + elapsed, _endFrame);

		auto tree = BMBase(*_treeBlueprint);

		for (const auto element : tree.children()) {
			if (element->active(frame)) {
				element->updateProperties(frame);
			}
		}

		LottieRasterRenderer renderer(&p);
		for (const auto element : tree.children()) {
			if (element->active(frame)) {
				element->render(renderer, frame);
			}
		}
	}
	return result;
}

int Animation::frameRate() const {
	return _frameRate;
}

crl::time Animation::duration() const {
	return (_endFrame - _startFrame) * crl::time(1000) / _frameRate;
}

void Animation::play(const PlaybackOptions &options) {
	_options = options;
	_started = crl::now();
}

void Animation::parse(const QByteArray &content) {
	const auto document = QJsonDocument::fromJson(content);
	const auto root = document.object();

	if (root.empty()) {
		_failed = true;
		return;
	}

	_startFrame = root.value(qstr("ip")).toVariant().toInt();
	_endFrame = root.value(qstr("op")).toVariant().toInt();
	_frameRate = root.value(qstr("fr")).toVariant().toInt();
	_realWidth = root.value(qstr("w")).toVariant().toReal();
	_realHeight = root.value(qstr("h")).toVariant().toReal();

	const auto markers = root.value(qstr("markers")).toArray();
	for (const auto &entry : markers) {
		const auto object = entry.toObject();
		const auto name = object.value(qstr("cm")).toString();
		const auto frame = object.value(qstr("tm")).toInt();
		_markers.emplace(name, frame);

		if (object.value(qstr("dr")).toInt()) {
			_unsupported = true;
		}
	}

	const auto assets = root.value(qstr("assets")).toArray();
	for (const auto &entry : assets) {
		if (const auto asset = BMAsset::construct(entry.toObject())) {
			_assetIndexById.emplace(asset->id(), _assets.size());
			_assets.emplace_back(asset);
		} else {
			_unsupported = true;
		}
	}

	if (root.value(qstr("chars")).toArray().count()) {
		_unsupported = true;
	}

	_treeBlueprint = std::make_unique<BMBase>();
	const auto blueprint = _treeBlueprint.get();
	const auto layers = root.value(QLatin1String("layers")).toArray();
	for (auto i = layers.end(); i != layers.begin();) {
		const auto &entry = *(--i);
		if (const auto layer = BMLayer::construct(entry.toObject())) {
			layer->setParent(blueprint);

			// Mask layers must be rendered before the layers they affect to
			// although they appear before in layer hierarchy. For this reason
			// move a mask after the affected layers, so it will be rendered first
			if (layer->isMaskLayer()) {
				blueprint->prependChild(layer);
			} else {
				blueprint->appendChild(layer);
			}
		} else {
			_unsupported = true;
		}
	}

	resolveAssets();
}

void Animation::resolveAssets() {
	if (_assets.empty()) {
		return;
	}

	std::function<BMAsset*(QString)> resolver = [&](const QString &refId)
	-> BMAsset* {
		const auto i = _assetIndexById.find(refId);
		if (i == end(_assetIndexById)) {
			return nullptr;
		}
		const auto result = _assets[i->second].get();
		result->resolveAssets(resolver);
		return result->clone();
	};
	for (const auto &asset : _assets) {
		asset->resolveAssets(resolver);
	}

	_treeBlueprint->resolveAssets([&](const QString &refId) {
		const auto i = _assetIndexById.find(refId);
		return (i != end(_assetIndexById))
			? _assets[i->second]->clone()
			: nullptr;
	});
}

} // namespace Lottie
