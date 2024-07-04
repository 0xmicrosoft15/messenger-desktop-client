/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {
struct GeoLocation;
} // namespace Core

namespace Platform {

void ResolveCurrentExactLocation(Fn<void(Core::GeoLocation)> callback);

} // namespace Platform
