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
#include "media/media_child_ffmpeg_loader.h"

#include "core/crash_reports.h"

namespace {

constexpr AVSampleFormat AudioToFormat = AV_SAMPLE_FMT_S16;
constexpr int64_t AudioToChannelLayout = AV_CH_LAYOUT_STEREO;
constexpr int32 AudioToChannels = 2;

bool IsPlanarFormat(int format) {
	return (format == AV_SAMPLE_FMT_U8P)
		|| (format == AV_SAMPLE_FMT_S16P)
		|| (format == AV_SAMPLE_FMT_S32P)
		|| (format == AV_SAMPLE_FMT_FLTP)
		|| (format == AV_SAMPLE_FMT_DBLP)
		|| (format == AV_SAMPLE_FMT_S64P);
}

} // namespace

VideoSoundData::~VideoSoundData() {
	if (context) {
		avcodec_close(context);
		avcodec_free_context(&context);
		context = nullptr;
	}
}

ChildFFMpegLoader::ChildFFMpegLoader(std::unique_ptr<VideoSoundData> &&data)
: AbstractAudioFFMpegLoader(
	FileLocation(),
	QByteArray(),
	base::byte_vector())
, _parentData(std::move(data)) {
}

bool ChildFFMpegLoader::open(TimeMs positionMs) {
	return initUsingContext(
		_parentData->context,
		_parentData->length,
		_parentData->frequency);
}

AudioPlayerLoader::ReadResult ChildFFMpegLoader::readMore(
		QByteArray &result,
		int64 &samplesAdded) {
	const auto readResult = readFromReadyContext(
		_parentData->context,
		result,
		samplesAdded);
	if (readResult != ReadResult::Wait) {
		return readResult;
	}

	if (_queue.isEmpty()) {
		return _eofReached ? ReadResult::EndOfFile : ReadResult::Wait;
	}

	AVPacket packet;
	FFMpeg::packetFromDataWrap(packet, _queue.dequeue());

	_eofReached = FFMpeg::isNullPacket(packet);
	if (_eofReached) {
		avcodec_send_packet(_parentData->context, nullptr); // drain
		return ReadResult::Ok;
	}

	auto res = avcodec_send_packet(_parentData->context, &packet);
	if (res < 0) {
		FFMpeg::freePacket(&packet);

		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Audio Error: Unable to avcodec_send_packet() file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		// There is a sample voice message where skipping such packet
		// results in a crash (read_access to nullptr) in swr_convert().
		if (res == AVERROR_INVALIDDATA) {
			return ReadResult::NotYet; // try to skip bad packet
		}
		return ReadResult::Error;
	}
	FFMpeg::freePacket(&packet);
	return ReadResult::Ok;
}

void ChildFFMpegLoader::enqueuePackets(QQueue<FFMpeg::AVPacketDataWrap> &packets) {
	_queue += std::move(packets);
	packets.clear();
}

ChildFFMpegLoader::~ChildFFMpegLoader() {
	for (auto &packetData : base::take(_queue)) {
		AVPacket packet;
		FFMpeg::packetFromDataWrap(packet, packetData);
		FFMpeg::freePacket(&packet);
	}
}
