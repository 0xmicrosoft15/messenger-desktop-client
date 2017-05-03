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
#pragma once

#include "base/timer.h"

namespace Media {
namespace Audio {

class Instance;

class Track {
public:
	Track(gsl::not_null<Instance*> instance);

	void fillFromData(base::byte_vector &&data);
	void fillFromFile(const FileLocation &location);
	void fillFromFile(const QString &filePath);

	void playOnce();
	void playInLoop();

	bool failed() const {
		return _failed;
	}

	void detachFromDevice();
	void reattachToDevice();
	bool isActive() const;
	void updateState();

	~Track();

private:
	void createSource();

	gsl::not_null<Instance*> _instance;

	bool _failed = false;
	bool _active = false;
	bool _looping = false;
	float64 _volume = 1.;

	int64 _samplesCount = 0;
	int32 _sampleRate = 0;
	base::byte_vector _samples;

	TimeMs _lengthMs = 0;

	int32 _alFormat = 0;
	int64 _alPosition = 0;
	uint32 _alSource = 0;
	uint32 _alBuffer = 0;

};

class Instance {
public:
	// Thread: Main.
	Instance();

	std::unique_ptr<Track> createTrack();

	base::Observable<Track*> &trackFinished() {
		return _trackFinished;
	}

	void detachTracks();
	void reattachTracks();
	bool hasActiveTracks() const;

	void scheduleDetachFromDevice();
	void scheduleDetachIfNotUsed();
	void stopDetachIfNotUsed();

	~Instance();

private:
	friend class Track;
	void registerTrack(Track *track);
	void unregisterTrack(Track *track);

private:
	std::set<Track*> _tracks;
	base::Observable<Track*> _trackFinished;

	base::Timer _updateTimer;

	base::Timer _detachFromDeviceTimer;
	bool _detachFromDeviceForce = false;

};

Instance &Current();

} // namespace Audio
} // namespace Media
