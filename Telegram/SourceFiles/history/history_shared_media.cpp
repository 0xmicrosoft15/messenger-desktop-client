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
#include "history/history_shared_media.h"

#include "auth_session.h"
#include "apiwrap.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"

namespace {

using Type = SharedMediaViewer::Type;

inline MediaOverviewType SharedMediaTypeToOverview(Type type) {
	switch (type) {
	case Type::Photo: return OverviewPhotos;
	case Type::Video: return OverviewVideos;
	case Type::MusicFile: return OverviewMusicFiles;
	case Type::File: return OverviewFiles;
	case Type::VoiceFile: return OverviewVoiceFiles;
	case Type::Link: return OverviewLinks;
	default: break;
	}
	return OverviewCount;
}

not_null<History*> GetActualHistory(not_null<History*> history) {
	if (auto to = history->peer->migrateTo()) {
		return App::history(to);
	}
	return history;
}

History *GetMigratedHistory(
		not_null<History*> passedHistory,
		not_null<History*> actualHistory) {
	if (actualHistory != passedHistory) {
		return passedHistory;
	} else if (auto from = actualHistory->peer->migrateFrom()) {
		return App::history(from);
	}
	return nullptr;
}

} // namespace

base::optional<Storage::SharedMediaType> SharedMediaOverviewType(
	Storage::SharedMediaType type) {
	if (SharedMediaTypeToOverview(type) != OverviewCount) {
		return type;
	}
	return base::none;
}

void SharedMediaShowOverview(
	Storage::SharedMediaType type,
	not_null<History*> history) {
	if (SharedMediaOverviewType(type)) {
		Ui::showPeerOverview(history, SharedMediaTypeToOverview(type));
	}
}

QString SharedMediaSlice::debug() const {
	auto before = _skippedBefore
		? (*_skippedBefore
			? ('(' + QString::number(*_skippedBefore) + ").. ")
			: QString())
		: QString(".. ");
	auto after = _skippedAfter
		? (*_skippedAfter
			? (" ..(" + QString::number(*_skippedAfter) + ')')
			: QString())
		: QString(" ..");
	auto middle = (size() > 2)
		? QString::number((*this)[0]) + " .. " + QString::number((*this)[size() - 1])
		: (size() > 1)
		? QString::number((*this)[0]) + ' ' + QString::number((*this)[1])
		: ((size() > 0) ? QString((*this)[0]) : QString());
	return before + middle + after;
}


SharedMediaViewer::SharedMediaViewer(
	Key key,
	int limitBefore,
	int limitAfter)
	: _key(key)
	, _limitBefore(limitBefore)
	, _limitAfter(limitAfter)
	, _data(_key) {
	Expects(IsServerMsgId(key.messageId) || (key.messageId == 0));
	Expects((key.messageId != 0) || (limitBefore == 0 && limitAfter == 0));
}

void SharedMediaViewer::start() {
	auto applyUpdateCallback = [this](auto &update) {
		this->applyUpdate(update);
	};
	subscribe(Auth().storage().sharedMediaSliceUpdated(), applyUpdateCallback);
	subscribe(Auth().storage().sharedMediaOneRemoved(), applyUpdateCallback);
	subscribe(Auth().storage().sharedMediaAllRemoved(), applyUpdateCallback);

	loadInitial();
}

void SharedMediaViewer::loadInitial() {
	auto weak = base::make_weak_unique(this);
	Auth().storage().query(Storage::SharedMediaQuery(
		_key,
		_limitBefore,
		_limitAfter), [weak](Storage::SharedMediaResult &&result) {
		if (weak) {
			weak->applyStoredResult(std::move(result));
		}
	});
}

void SharedMediaViewer::applyStoredResult(Storage::SharedMediaResult &&result) {
	mergeSliceData(
		result.count,
		result.messageIds,
		result.skippedBefore,
		result.skippedAfter);
}

void SharedMediaViewer::mergeSliceData(
		base::optional<int> count,
		const base::flat_set<MsgId> &messageIds,
		base::optional<int> skippedBefore,
		base::optional<int> skippedAfter) {
	if (messageIds.empty()) {
		if (count && _data._fullCount != count) {
			_data._fullCount = count;
			if (*_data._fullCount <= _data.size()) {
				_data._fullCount = _data.size();
				_data._skippedBefore = _data._skippedAfter = 0;
			}
			updated.notify(_data);
		}
		sliceToLimits();
		return;
	}
	if (count) {
		_data._fullCount = count;
	}
	auto wasMinId = _data._ids.empty() ? -1 : _data._ids.front();
	auto wasMaxId = _data._ids.empty() ? -1 : _data._ids.back();
	_data._ids.merge(messageIds.begin(), messageIds.end());

	auto adjustSkippedBefore = [&](MsgId oldId, int oldSkippedBefore) {
		auto it = _data._ids.find(oldId);
		Assert(it != _data._ids.end());
		_data._skippedBefore = oldSkippedBefore - (it - _data._ids.begin());
		accumulate_max(*_data._skippedBefore, 0);
	};
	if (skippedBefore) {
		adjustSkippedBefore(messageIds.front(), *skippedBefore);
	} else if (wasMinId >= 0 && _data._skippedBefore) {
		adjustSkippedBefore(wasMinId, *_data._skippedBefore);
	} else {
		_data._skippedBefore = base::none;
	}

	auto adjustSkippedAfter = [&](MsgId oldId, int oldSkippedAfter) {
		auto it = _data._ids.find(oldId);
		Assert(it != _data._ids.end());
		_data._skippedAfter = oldSkippedAfter - (_data._ids.end() - it - 1);
		accumulate_max(*_data._skippedAfter, 0);
	};
	if (skippedAfter) {
		adjustSkippedAfter(messageIds.back(), *skippedAfter);
	} else if (wasMaxId >= 0 && _data._skippedAfter) {
		adjustSkippedAfter(wasMaxId, *_data._skippedAfter);
	} else {
		_data._skippedAfter = base::none;
	}

	if (_data._fullCount) {
		if (_data._skippedBefore && !_data._skippedAfter) {
			_data._skippedAfter = *_data._fullCount
				- *_data._skippedBefore
				- int(_data._ids.size());
		} else if (_data._skippedAfter && !_data._skippedBefore) {
			_data._skippedBefore = *_data._fullCount
				- *_data._skippedAfter
				- int(_data._ids.size());
		}
	}

	sliceToLimits();

	updated.notify(_data);
}

void SharedMediaViewer::applyUpdate(const SliceUpdate &update) {
	if (update.peerId != _key.peerId || update.type != _key.type) {
		return;
	}
	auto intersects = [](MsgRange range1, MsgRange range2) {
		return (range1.from <= range2.till) && (range2.from <= range1.till);
	};
	if (!intersects(update.range, {
		_data._ids.empty() ? _key.messageId : _data._ids.front(),
		_data._ids.empty() ? _key.messageId : _data._ids.back() })) {
		return;
	}
	auto skippedBefore = (update.range.from == 0)
		? 0
		: base::optional<int> {};
	auto skippedAfter = (update.range.till == ServerMaxMsgId)
		? 0
		: base::optional<int> {};
	mergeSliceData(
		update.count,
		update.messages ? *update.messages : base::flat_set<MsgId> {},
		skippedBefore,
		skippedAfter);
}

void SharedMediaViewer::applyUpdate(const OneRemoved &update) {
	if (update.peerId != _key.peerId || !update.types.test(_key.type)) {
		return;
	}
	auto changed = false;
	if (_data._fullCount && *_data._fullCount > 0) {
		--*_data._fullCount;
		changed = true;
	}
	if (_data._ids.contains(update.messageId)) {
		_data._ids.remove(update.messageId);
		changed = true;
	} else if (!_data._ids.empty()) {
		if (_data._ids.front() > update.messageId
			&& _data._skippedBefore
			&& *_data._skippedBefore > 0) {
			--*_data._skippedBefore;
			changed = true;
		} else if (_data._ids.back() < update.messageId
			&& _data._skippedAfter
			&& *_data._skippedAfter > 0) {
			--*_data._skippedAfter;
			changed = true;
		}
	}
	if (changed) {
		updated.notify(_data);
	}
}

void SharedMediaViewer::applyUpdate(const AllRemoved &update) {
	if (update.peerId != _key.peerId) {
		return;
	}
	_data = SharedMediaSlice(_key, 0);
	updated.notify(_data);
}

void SharedMediaViewer::sliceToLimits() {
	auto aroundIt = base::lower_bound(_data._ids, _key.messageId);
	auto removeFromBegin = (aroundIt - _data._ids.begin() - _limitBefore);
	auto removeFromEnd = (_data._ids.end() - aroundIt - _limitAfter - 1);
	if (removeFromBegin > 0) {
		_data._ids.erase(_data._ids.begin(), _data._ids.begin() + removeFromBegin);
		if (_data._skippedBefore) {
			*_data._skippedBefore += removeFromBegin;
		}
	} else if (removeFromBegin < 0 && (!_data._skippedBefore || *_data._skippedBefore > 0)) {
		requestMessages(RequestDirection::Before);
	}
	if (removeFromEnd > 0) {
		_data._ids.erase(_data._ids.end() - removeFromEnd, _data._ids.end());
		if (_data._skippedAfter) {
			*_data._skippedAfter += removeFromEnd;
		}
	} else if (removeFromEnd < 0 && (!_data._skippedAfter || *_data._skippedAfter > 0)) {
		requestMessages(RequestDirection::After);
	}
}

void SharedMediaViewer::requestMessages(RequestDirection direction) {
	using SliceType = ApiWrap::SliceType;
	auto requestAroundData = [&]() -> std::pair<MsgId, SliceType> {
		if (_data._ids.empty()) {
			return { _key.messageId, SliceType::Around };
		} else if (direction == RequestDirection::Before) {
			return { _data._ids.front(), SliceType::Before };
		}
		return { _data._ids.back(), SliceType::After };
	}();
	Auth().api().requestSharedMedia(
		App::peer(_key.peerId),
		_key.type,
		requestAroundData.first,
		requestAroundData.second);
}

SharedMediaViewerMerged::SharedMediaViewerMerged(
	Key key,
	int limitBefore,
	int limitAfter)
	: _key(key)
	, _limitBefore(limitBefore)
	, _limitAfter(limitAfter)
	, _part(PartKey(_key), _limitBefore, _limitAfter)
	, _migrated(MigratedViewer(_key, _limitBefore, _limitAfter))
	, _data(_key, SharedMediaSlice(PartKey(_key)), MigratedSlice(_key)) {
	Expects(IsServerMsgId(key.universalId)
		|| (key.universalId == 0)
		|| (IsServerMsgId(-key.universalId) && key.migratedPeerId != 0));
	Expects((key.universalId != 0) || (limitBefore == 0 && limitAfter == 0));
}

SharedMediaSlice::Key SharedMediaViewerMerged::PartKey(const Key &key) {
	return {
		key.peerId,
		key.type,
		(key.universalId < 0) ? 1 : key.universalId
	};
}

SharedMediaSlice::Key SharedMediaViewerMerged::MigratedKey(const Key &key) {
	return {
		key.migratedPeerId,
		key.type,
		(key.universalId <= 0) ? (-key.universalId) : (ServerMaxMsgId - 1)
	};
}

std::unique_ptr<SharedMediaViewer> SharedMediaViewerMerged::MigratedViewer(
		const Key &key,
		int limitBefore,
		int limitAfter) {
	return key.migratedPeerId
		? std::make_unique<SharedMediaViewer>(
			MigratedKey(key),
			limitBefore,
			limitAfter)
		: nullptr;
}

base::optional<SharedMediaSlice> SharedMediaViewerMerged::MigratedSlice(
		const Key &key) {
	if (!key.migratedPeerId) {
		return base::none;
	}
	return SharedMediaSlice(MigratedKey(key));
}

void SharedMediaViewerMerged::start() {
	subscribe(_part.updated, [this](const SharedMediaSlice &update) {
		_data = SharedMediaSliceMerged(_key, update, _data._migrated);
		updated.notify(_data);
	});
	if (_migrated) {
		subscribe(_migrated->updated, [this](const SharedMediaSlice &update) {
			_data = SharedMediaSliceMerged(_key, _data._part, update);
			updated.notify(_data);
		});
	}
	_part.start();
	if (_migrated) {
		_migrated->start();
	}
}
