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
#pragma once

enum EntityInTextType {
	EntityInTextInvalid = 0,

	EntityInTextUrl,
	EntityInTextCustomUrl,
	EntityInTextEmail,
	EntityInTextHashtag,
	EntityInTextMention,
	EntityInTextMentionName,
	EntityInTextBotCommand,

	EntityInTextBold,
	EntityInTextItalic,
	EntityInTextCode, // inline
	EntityInTextPre,  // block
};

class EntityInText;
using EntitiesInText = QList<EntityInText>;

class EntityInText {
public:
	EntityInText(EntityInTextType type, int offset, int length, const QString &data = QString())
		: _type(type)
		, _offset(offset)
		, _length(length)
		, _data(data) {
	}

	EntityInTextType type() const {
		return _type;
	}
	int offset() const {
		return _offset;
	}
	int length() const {
		return _length;
	}
	QString data() const {
		return _data;
	}

	void extendToLeft(int extent) {
		_offset -= extent;
		_length += extent;
	}
	void shrinkFromRight(int shrink) {
		_length -= shrink;
	}
	void shiftLeft(int shift) {
		_offset -= shift;
		if (_offset < 0) {
			_length += _offset;
			_offset = 0;
			if (_length < 0) {
				_length = 0;
			}
		}
	}
	void updateTextEnd(int textEnd) {
		if (_offset > textEnd) {
			_offset = textEnd;
			_length = 0;
		} else if (_offset + _length > textEnd) {
			_length = textEnd - _offset;
		}
	}

	static int firstMonospaceOffset(const EntitiesInText &entities, int textLength) {
		int result = textLength;
		for_const (auto &entity, entities) {
			if (entity.type() == EntityInTextPre || entity.type() == EntityInTextCode) {
				accumulate_min(result, entity.offset());
			}
		}
		return result;
	}

	explicit operator bool() const {
		return type() != EntityInTextInvalid;
	}

private:
	EntityInTextType _type;
	int _offset, _length;
	QString _data;

};
typedef QList<EntityInText> EntitiesInText;

// text preprocess
QString textClean(const QString &text);
QString textRichPrepare(const QString &text);
QString textOneLine(const QString &text, bool trim = true, bool rich = false);
QString textAccentFold(const QString &text);
QString textSearchKey(const QString &text);
bool textSplit(QString &sendingText, EntitiesInText &sendingEntities, QString &leftText, EntitiesInText &leftEntities, int32 limit);

enum {
	TextParseMultiline = 0x001,
	TextParseLinks = 0x002,
	TextParseRichText = 0x004,
	TextParseMentions = 0x008,
	TextParseHashtags = 0x010,
	TextParseBotCommands = 0x020,
	TextParseMono = 0x040,

	TextTwitterMentions = 0x100,
	TextTwitterHashtags = 0x200,
	TextInstagramMentions = 0x400,
	TextInstagramHashtags = 0x800,
};

EntitiesInText entitiesFromMTP(const QVector<MTPMessageEntity> &entities);
MTPVector<MTPMessageEntity> linksToMTP(const EntitiesInText &links, bool sending = false);

EntitiesInText textParseEntities(QString &text, int32 flags, bool rich = false); // changes text if (flags & TextParseMono)
QString textApplyEntities(const QString &text, const EntitiesInText &entities);

QString prepareTextWithEntities(QString result, EntitiesInText &entities, int32 flags);

inline QString prepareText(QString result, bool checkLinks = false) {
	EntitiesInText entities;
	return prepareTextWithEntities(result, entities, checkLinks ? (TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands) : 0);
}

void moveStringPart(QChar *start, int32 &to, int32 &from, int32 count, EntitiesInText &entities);

// replace bad symbols with space and remove \r
void cleanTextWithEntities(QString &result, EntitiesInText &entities);
void trimTextWithEntities(QString &result, EntitiesInText &entities);