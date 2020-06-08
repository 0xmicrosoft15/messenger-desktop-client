/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/message_field.h"

#include "history/history_widget.h"
#include "history/history.h" // History::session
#include "history/history_item.h" // HistoryItem::originalText
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/event_filter.h"
#include "boxes/abstract_box.h"
#include "core/shortcuts.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/popup_menu.h"
#include "ui/ui_utility.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_history.h"

#include <QtCore/QMimeData>
#include <QtCore/QStack>
#include <QtGui/QGuiApplication>
#include <QtGui/QTextBlock>
#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>

namespace {

using namespace Ui::Text;

using EditLinkAction = Ui::InputField::EditLinkAction;
using EditLinkSelection = Ui::InputField::EditLinkSelection;

constexpr auto kParseLinksTimeout = crl::time(1000);

// For mention tags save and validate userId, ignore tags for different userId.
class FieldTagMimeProcessor : public Ui::InputField::TagMimeProcessor {
public:
	explicit FieldTagMimeProcessor(
		not_null<Window::SessionController*> controller);

	QString tagFromMimeTag(const QString &mimeTag) override;

private:
	const not_null<Window::SessionController*> _controller;

};

class EditLinkBox : public Ui::BoxContent {
public:
	EditLinkBox(
		QWidget*,
		not_null<Main::Session*> session,
		const QString &text,
		const QString &link,
		Fn<void(QString, QString)> callback);

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	const not_null<Main::Session*> _session;
	QString _startText;
	QString _startLink;
	Fn<void(QString, QString)> _callback;
	Fn<void()> _setInnerFocus;

};

FieldTagMimeProcessor::FieldTagMimeProcessor(
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

QString FieldTagMimeProcessor::tagFromMimeTag(const QString &mimeTag) {
	if (TextUtilities::IsMentionLink(mimeTag)) {
		const auto userId = _controller->session().userId();
		auto match = QRegularExpression(":(\\d+)$").match(mimeTag);
		if (!match.hasMatch()
			|| match.capturedRef(1).toInt() != userId) {
			return QString();
		}
		return mimeTag.mid(0, mimeTag.size() - match.capturedLength());
	}
	return mimeTag;
}

//bool ValidateUrl(const QString &value) {
//	const auto match = qthelp::RegExpDomain().match(value);
//	if (!match.hasMatch() || match.capturedStart() != 0) {
//		return false;
//	}
//	const auto protocolMatch = RegExpProtocol().match(value);
//	return protocolMatch.hasMatch()
//		&& IsGoodProtocol(protocolMatch.captured(1));
//}

EditLinkBox::EditLinkBox(
	QWidget*,
	not_null<Main::Session*> session,
	const QString &text,
	const QString &link,
	Fn<void(QString, QString)> callback)
: _session(session)
, _startText(text)
, _startLink(link)
, _callback(std::move(callback)) {
	Expects(_callback != nullptr);
}

void EditLinkBox::setInnerFocus() {
	Expects(_setInnerFocus != nullptr);

	_setInnerFocus();
}

void EditLinkBox::prepare() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto text = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			tr::lng_formatting_link_text(),
			_startText),
		st::markdownLinkFieldPadding);
	text->setInstantReplaces(Ui::InstantReplaces::Default());
	text->setInstantReplacesEnabled(
		_session->settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		text,
		_session);
	InitSpellchecker(_session, text);

	const auto url = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			tr::lng_formatting_link_url(),
			_startLink.trimmed()),
		st::markdownLinkFieldPadding);

	const auto submit = [=] {
		const auto linkText = text->getLastText();
		const auto linkUrl = qthelp::validate_url(url->getLastText());
		if (linkText.isEmpty()) {
			text->showError();
			return;
		} else if (linkUrl.isEmpty()) {
			url->showError();
			return;
		}
		const auto weak = Ui::MakeWeak(this);
		_callback(linkText, linkUrl);
		if (weak) {
			closeBox();
		}
	};

	connect(text, &Ui::InputField::submitted, [=] {
		url->setFocusFast();
	});
	connect(url, &Ui::InputField::submitted, [=] {
		if (text->getLastText().isEmpty()) {
			text->setFocusFast();
		} else {
			submit();
		}
	});

	setTitle(url->getLastText().isEmpty()
		? tr::lng_formatting_link_create_title()
		: tr::lng_formatting_link_edit_title());

	addButton(tr::lng_formatting_link_create(), submit);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	content->resizeToWidth(st::boxWidth);
	content->moveToLeft(0, 0);
	setDimensions(st::boxWidth, content->height());

	_setInnerFocus = [=] {
		(_startText.isEmpty() ? text : url)->setFocusFast();
	};
}

TextWithEntities StripSupportHashtag(TextWithEntities &&text) {
	static const auto expression = QRegularExpression(
		qsl("\\n?#tsf[a-z0-9_-]*[\\s#a-z0-9_-]*$"),
		QRegularExpression::CaseInsensitiveOption);
	const auto match = expression.match(text.text);
	if (!match.hasMatch()) {
		return std::move(text);
	}
	text.text.chop(match.capturedLength());
	const auto length = text.text.size();
	if (!length) {
		return TextWithEntities();
	}
	for (auto i = text.entities.begin(); i != text.entities.end();) {
		auto &entity = *i;
		if (entity.offset() >= length) {
			i = text.entities.erase(i);
			continue;
		} else if (entity.offset() + entity.length() > length) {
			entity.shrinkFromRight(length - entity.offset());
		}
		++i;
	}
	return std::move(text);
}

} // namespace

QString PrepareMentionTag(not_null<UserData*> user) {
	return TextUtilities::kMentionTagStart
		+ QString::number(user->bareId())
		+ '.'
		+ QString::number(user->accessHash());
}

TextWithTags PrepareEditText(not_null<HistoryItem*> item) {
	const auto original = item->history()->session().supportMode()
		? StripSupportHashtag(item->originalText())
		: item->originalText();
	return TextWithTags{
		original.text,
		TextUtilities::ConvertEntitiesToTextTags(original.entities)
	};
}

Fn<bool(
	Ui::InputField::EditLinkSelection selection,
	QString text,
	QString link,
	EditLinkAction action)> DefaultEditLinkCallback(
		not_null<Main::Session*> session,
		not_null<Ui::InputField*> field) {
	const auto weak = Ui::MakeWeak(field);
	return [=](
			EditLinkSelection selection,
			QString text,
			QString link,
			EditLinkAction action) {
		if (action == EditLinkAction::Check) {
			return Ui::InputField::IsValidMarkdownLink(link)
				&& !TextUtilities::IsMentionLink(link);
		}
		Ui::show(Box<EditLinkBox>(session, text, link, [=](
				const QString &text,
				const QString &link) {
			if (const auto strong = weak.data()) {
				strong->commitMarkdownLinkEdit(selection, text, link);
			}
		}), Ui::LayerOption::KeepOther);
		return true;
	};
}

void InitMessageField(
		not_null<Window::SessionController*> controller,
		not_null<Ui::InputField*> field) {
	field->setMinHeight(st::historySendSize.height() - 2 * st::historySendPadding);
	field->setMaxHeight(st::historyComposeFieldMaxHeight);

	field->setTagMimeProcessor(
		std::make_unique<FieldTagMimeProcessor>(controller));

	field->document()->setDocumentMargin(4.);
	field->setAdditionalMargin(style::ConvertScale(4) - 4);

	field->customTab(true);
	field->setInstantReplaces(Ui::InstantReplaces::Default());
	field->setInstantReplacesEnabled(
		controller->session().settings().replaceEmojiValue());
	field->setMarkdownReplacesEnabled(rpl::single(true));
	field->setEditLinkCallback(
		DefaultEditLinkCallback(&controller->session(), field));
}

void InitSpellchecker(
		not_null<Main::Session*> session,
		not_null<Ui::InputField*> field) {
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	const auto s = Ui::CreateChild<Spellchecker::SpellingHighlighter>(
		field.get(),
		session->settings().spellcheckerEnabledValue(),
		Spellchecker::SpellingHighlighter::CustomContextMenuItem{
			tr::lng_settings_manage_dictionaries(tr::now),
			[=] { Ui::show(Box<Ui::ManageDictionariesBox>(session)); }
		});
	field->setExtendedContextMenu(s->contextMenuCreated());
#endif // TDESKTOP_DISABLE_SPELLCHECK
}

bool HasSendText(not_null<const Ui::InputField*> field) {
	const auto &text = field->getTextWithTags().text;
	for (const auto ch : text) {
		const auto code = ch.unicode();
		if (code != ' '
			&& code != '\n'
			&& code != '\r'
			&& !IsReplacedBySpace(code)) {
			return true;
		}
	}
	return false;
}

InlineBotQuery ParseInlineBotQuery(
		not_null<Main::Session*> session,
		not_null<const Ui::InputField*> field) {
	auto result = InlineBotQuery();

	const auto &full = field->getTextWithTags();
	const auto &text = full.text;
	const auto textLength = text.size();

	auto inlineUsernameStart = 1;
	auto inlineUsernameLength = 0;
	if (textLength > 2 && text[0] == '@' && text[1].isLetter()) {
		inlineUsernameLength = 1;
		for (auto i = inlineUsernameStart + 1; i != textLength; ++i) {
			const auto ch = text[i];
			if (ch.isLetterOrNumber() || ch.unicode() == '_') {
				++inlineUsernameLength;
				continue;
			} else if (!ch.isSpace()) {
				inlineUsernameLength = 0;
			}
			break;
		}
		auto inlineUsernameEnd = inlineUsernameStart + inlineUsernameLength;
		auto inlineUsernameEqualsText = (inlineUsernameEnd == textLength);
		auto validInlineUsername = false;
		if (inlineUsernameEqualsText) {
			validInlineUsername = text.endsWith(qstr("bot"));
		} else if (inlineUsernameEnd < textLength && inlineUsernameLength) {
			validInlineUsername = text[inlineUsernameEnd].isSpace();
		}
		if (validInlineUsername) {
			if (!full.tags.isEmpty()
				&& (full.tags.front().offset
					< inlineUsernameStart + inlineUsernameLength)) {
				return InlineBotQuery();
			}
			auto username = text.midRef(inlineUsernameStart, inlineUsernameLength);
			if (username != result.username) {
				result.username = username.toString();
				if (const auto peer = session->data().peerByUsername(result.username)) {
					if (const auto user = peer->asUser()) {
						result.bot = peer->asUser();
					} else {
						result.bot = nullptr;
					}
					result.lookingUpBot = false;
				} else {
					result.bot = nullptr;
					result.lookingUpBot = true;
				}
			}
			if (result.lookingUpBot) {
				result.query = QString();
				return result;
			} else if (result.bot
				&& (!result.bot->isBot()
					|| result.bot->botInfo->inlinePlaceholder.isEmpty())) {
				result.bot = nullptr;
			} else {
				result.query = inlineUsernameEqualsText
					? QString()
					: text.mid(inlineUsernameEnd + 1);
				return result;
			}
		} else {
			inlineUsernameLength = 0;
		}
	}
	if (inlineUsernameLength < 3) {
		result.bot = nullptr;
		result.username = QString();
	}
	result.query = QString();
	return result;
}

AutocompleteQuery ParseMentionHashtagBotCommandQuery(
		not_null<const Ui::InputField*> field) {
	auto result = AutocompleteQuery();

	const auto cursor = field->textCursor();
	if (cursor.hasSelection()) {
		return result;
	}

	const auto position = cursor.position();
	const auto document = field->document();
	const auto block = document->findBlock(position);
	for (auto item = block.begin(); !item.atEnd(); ++item) {
		const auto fragment = item.fragment();
		if (!fragment.isValid()) {
			continue;
		}

		const auto fragmentPosition = fragment.position();
		const auto fragmentEnd = fragmentPosition + fragment.length();
		if (fragmentPosition >= position || fragmentEnd < position) {
			continue;
		}

		const auto format = fragment.charFormat();
		if (format.isImageFormat()) {
			continue;
		}

		bool mentionInCommand = false;
		const auto text = fragment.text();
		for (auto i = position - fragmentPosition; i != 0; --i) {
			if (text[i - 1] == '@') {
				if ((position - fragmentPosition - i < 1 || text[i].isLetter()) && (i < 2 || !(text[i - 2].isLetterOrNumber() || text[i - 2] == '_'))) {
					result.fromStart = (i == 1) && (fragmentPosition == 0);
					result.query = text.mid(i - 1, position - fragmentPosition - i + 1);
				} else if ((position - fragmentPosition - i < 1 || text[i].isLetter()) && i > 2 && (text[i - 2].isLetterOrNumber() || text[i - 2] == '_') && !mentionInCommand) {
					mentionInCommand = true;
					--i;
					continue;
				}
				return result;
			} else if (text[i - 1] == '#') {
				if (i < 2 || !(text[i - 2].isLetterOrNumber() || text[i - 2] == '_')) {
					result.fromStart = (i == 1) && (fragmentPosition == 0);
					result.query = text.mid(i - 1, position - fragmentPosition - i + 1);
				}
				return result;
			} else if (text[i - 1] == '/') {
				if (i < 2) {
					result.fromStart = (i == 1) && (fragmentPosition == 0);
					result.query = text.mid(i - 1, position - fragmentPosition - i + 1);
				}
				return result;
			}
			if (position - fragmentPosition - i > 127 || (!mentionInCommand && (position - fragmentPosition - i > 63))) {
				break;
			}
			if (!text[i - 1].isLetterOrNumber() && text[i - 1] != '_') {
				break;
			}
		}
		break;
	}
	return result;
}

MessageLinksParser::MessageLinksParser(not_null<Ui::InputField*> field)
: _field(field)
, _timer([=] { parse(); }) {
	_connection = QObject::connect(_field, &Ui::InputField::changed, [=] {
		const auto length = _field->getTextWithTags().text.size();
		const auto timeout = (std::abs(length - _lastLength) > 2)
			? 0
			: kParseLinksTimeout;
		if (!_timer.isActive() || timeout < _timer.remainingTime()) {
			_timer.callOnce(timeout);
		}
		_lastLength = length;
	});
	_field->installEventFilter(this);
}

bool MessageLinksParser::eventFilter(QObject *object, QEvent *event) {
	if (object == _field) {
		if (event->type() == QEvent::KeyPress) {
			const auto text = static_cast<QKeyEvent*>(event)->text();
			if (!text.isEmpty() && text.size() < 3) {
				const auto ch = text[0];
				if (false
					|| ch == '\n'
					|| ch == '\r'
					|| ch.isSpace()
					|| ch == QChar::LineSeparator) {
					_timer.callOnce(0);
				}
			}
		} else if (event->type() == QEvent::Drop) {
			_timer.callOnce(0);
		}
	}
	return QObject::eventFilter(object, event);
}

const rpl::variable<QStringList> &MessageLinksParser::list() const {
	return _list;
}

void MessageLinksParser::parse() {
	const auto &textWithTags = _field->getTextWithTags();
	const auto &text = textWithTags.text;
	const auto &tags = textWithTags.tags;
	const auto &markdownTags = _field->getMarkdownTags();
	if (text.isEmpty()) {
		_list = QStringList();
		return;
	}
	const auto tagCanIntersectWithLink = [](const QString &tag) {
		return (tag == Ui::InputField::kTagBold)
			|| (tag == Ui::InputField::kTagItalic)
			|| (tag == Ui::InputField::kTagUnderline)
			|| (tag == Ui::InputField::kTagStrikeOut);
	};

	auto ranges = QVector<LinkRange>();

	auto tag = tags.begin();
	const auto tagsEnd = tags.end();
	const auto processTag = [&] {
		Expects(tag != tagsEnd);

		if (Ui::InputField::IsValidMarkdownLink(tag->id)
			&& !TextUtilities::IsMentionLink(tag->id)) {
			ranges.push_back({ tag->offset, tag->length, tag->id });
		}
		++tag;
	};
	const auto processTagsBefore = [&](int offset) {
		while (tag != tagsEnd
			&& (tag->offset + tag->length <= offset
				|| tagCanIntersectWithLink(tag->id))) {
			processTag();
		}
	};
	const auto hasTagsIntersection = [&](int till) {
		if (tag == tagsEnd || tag->offset >= till) {
			return false;
		}
		while (tag != tagsEnd && tag->offset < till) {
			processTag();
		}
		return true;
	};

	auto markdownTag = markdownTags.begin();
	const auto markdownTagsEnd = markdownTags.end();
	const auto markdownTagsAllow = [&](int from, int length) {
		while (markdownTag != markdownTagsEnd
			&& (markdownTag->adjustedStart
				+ markdownTag->adjustedLength <= from
				|| !markdownTag->closed
				|| tagCanIntersectWithLink(markdownTag->tag))) {
			++markdownTag;
		}
		if (markdownTag == markdownTagsEnd
			|| markdownTag->adjustedStart >= from + length) {
			return true;
		}
		// Ignore http-links that are completely inside some tags.
		// This will allow sending http://test.com/__test__/test correctly.
		return (markdownTag->adjustedStart > from)
			|| (markdownTag->adjustedStart
				+ markdownTag->adjustedLength < from + length);
	};

	const auto len = text.size();
	const QChar *start = text.unicode(), *end = start + text.size();
	for (auto offset = 0, matchOffset = offset; offset < len;) {
		auto m = qthelp::RegExpDomain().match(text, matchOffset);
		if (!m.hasMatch()) break;

		auto domainOffset = m.capturedStart();

		auto protocol = m.captured(1).toLower();
		auto topDomain = m.captured(3).toLower();
		auto isProtocolValid = protocol.isEmpty() || TextUtilities::IsValidProtocol(protocol);
		auto isTopDomainValid = !protocol.isEmpty() || TextUtilities::IsValidTopDomain(topDomain);

		if (protocol.isEmpty() && domainOffset > offset + 1 && *(start + domainOffset - 1) == QChar('@')) {
			auto forMailName = text.mid(offset, domainOffset - offset - 1);
			auto mMailName = TextUtilities::RegExpMailNameAtEnd().match(forMailName);
			if (mMailName.hasMatch()) {
				offset = matchOffset = m.capturedEnd();
				continue;
			}
		}
		if (!isProtocolValid || !isTopDomainValid) {
			offset = matchOffset = m.capturedEnd();
			continue;
		}

		QStack<const QChar*> parenth;
		const QChar *domainEnd = start + m.capturedEnd(), *p = domainEnd;
		for (; p < end; ++p) {
			QChar ch(*p);
			if (IsLinkEnd(ch)) {
				break; // link finished
			} else if (IsAlmostLinkEnd(ch)) {
				const QChar *endTest = p + 1;
				while (endTest < end && IsAlmostLinkEnd(*endTest)) {
					++endTest;
				}
				if (endTest >= end || IsLinkEnd(*endTest)) {
					break; // link finished at p
				}
				p = endTest;
				ch = *p;
			}
			if (ch == '(' || ch == '[' || ch == '{' || ch == '<') {
				parenth.push(p);
			} else if (ch == ')' || ch == ']' || ch == '}' || ch == '>') {
				if (parenth.isEmpty()) break;
				const QChar *q = parenth.pop(), open(*q);
				if ((ch == ')' && open != '(') || (ch == ']' && open != '[') || (ch == '}' && open != '{') || (ch == '>' && open != '<')) {
					p = q;
					break;
				}
			}
		}
		if (p > domainEnd) { // check, that domain ended
			if (domainEnd->unicode() != '/' && domainEnd->unicode() != '?') {
				matchOffset = domainEnd - start;
				continue;
			}
		}
		const auto range = LinkRange {
			domainOffset,
			static_cast<int>(p - start - domainOffset),
			QString()
		};
		processTagsBefore(domainOffset);
		if (!hasTagsIntersection(range.start + range.length)) {
			if (markdownTagsAllow(range.start, range.length)) {
				ranges.push_back(range);
			}
		}
		offset = matchOffset = p - start;
	}
	processTagsBefore(QFIXED_MAX);

	apply(text, ranges);
}

void MessageLinksParser::apply(
		const QString &text,
		const QVector<LinkRange> &ranges) {
	const auto count = int(ranges.size());
	const auto current = _list.current();
	const auto computeLink = [&](const LinkRange &range) {
		return range.custom.isEmpty()
			? text.midRef(range.start, range.length)
			: range.custom.midRef(0);
	};
	const auto changed = [&] {
		if (current.size() != count) {
			return true;
		}
		for (auto i = 0; i != count; ++i) {
			if (computeLink(ranges[i]) != current[i]) {
				return true;
			}
		}
		return false;
	}();
	if (!changed) {
		return;
	}
	auto parsed = QStringList();
	parsed.reserve(count);
	for (const auto &range : ranges) {
		parsed.push_back(computeLink(range).toString());
	}
	_list = std::move(parsed);
}

void SetupSendMenuAndShortcuts(
		not_null<Ui::RpWidget*> button,
		Fn<SendMenuType()> type,
		Fn<void()> silent,
		Fn<void()> schedule) {
	if (!silent && !schedule) {
		return;
	}
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		const auto now = type();
		if (now == SendMenuType::Disabled
			|| (!silent && now == SendMenuType::SilentOnly)) {
			return false;
		}

		*menu = base::make_unique_q<Ui::PopupMenu>(button);
		if (silent && now != SendMenuType::Reminder) {
			(*menu)->addAction(tr::lng_send_silent_message(tr::now), silent);
		}
		if (schedule && now != SendMenuType::SilentOnly) {
			(*menu)->addAction(
				(now == SendMenuType::Reminder
					? tr::lng_reminder_message(tr::now)
					: tr::lng_schedule_message(tr::now)),
				schedule);
		}
		(*menu)->popup(QCursor::pos());
		return true;
	};
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu && showMenu()) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		const auto now = type();
		if (now == SendMenuType::Disabled
			|| (!silent && now == SendMenuType::SilentOnly)) {
			return;
		}
		(silent
			&& (now != SendMenuType::Reminder)
			&& request->check(Command::SendSilentMessage)
			&& request->handle([=] {
				silent();
				return true;
			}))
		||
		(schedule
			&& (now != SendMenuType::SilentOnly)
			&& request->check(Command::ScheduleMessage)
			&& request->handle([=] {
				schedule();
				return true;
			}))
		||
		(request->check(Command::JustSendMessage) && request->handle([=] {
			const auto post = [&](QEvent::Type type) {
				QApplication::postEvent(
					button,
					new QMouseEvent(
						type,
						QPointF(0, 0),
						Qt::LeftButton,
						Qt::LeftButton,
						Qt::NoModifier));
			};
			post(QEvent::MouseButtonPress);
			post(QEvent::MouseButtonRelease);
			return true;
		}));
	}, button->lifetime());
}
