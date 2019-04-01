/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/panel_animation.h"
#include "ui/rp_widget.h"
#include "base/unique_qptr.h"
#include "base/timer.h"

namespace Ui {

class InnerDropdown;
class InputField;

namespace Emoji {

class SuggestionsWidget final : public Ui::RpWidget {
public:
	SuggestionsWidget(QWidget *parent);

	void showWithQuery(const QString &query, bool force = false);
	void selectFirstResult();
	bool handleKeyEvent(int key);

	rpl::producer<bool> toggleAnimated() const;
	rpl::producer<QString> triggered() const;

private:
	struct Row {
		Row(not_null<EmojiPtr> emoji, const QString &replacement);

		not_null<EmojiPtr> emoji;
		QString replacement;
	};

	bool eventHook(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void scrollByWheelEvent(not_null<QWheelEvent*> e);
	void paintFadings(Painter &p) const;

	std::vector<Row> getRowsByQuery() const;
	void resizeToRows();
	void setSelected(int selected);
	void setPressed(int pressed);
	void clearMouseSelection();
	void clearSelection();
	void updateSelectedItem();
	void updateItem(int index);
	[[nodiscard]] QRect inner() const;
	[[nodiscard]] QPoint innerShift() const;
	[[nodiscard]] QPoint mapToInner(QPoint globalPosition) const;
	void selectByMouse(QPoint globalPosition);
	bool triggerSelectedRow() const;
	void triggerRow(const Row &row) const;

	QString _query;
	std::vector<Row> _rows;

	std::optional<QPoint> _lastMousePosition;
	bool _mouseSelection = false;
	int _selected = -1;
	int _pressed = -1;

	int _scroll = 0;
	int _scrollMax = 0;
	int _oneWidth = 0;
	QMargins _padding;

	QPoint _mousePressPosition;
	int _dragScrollStart = -1;

	rpl::event_stream<bool> _toggleAnimated;
	rpl::event_stream<QString> _triggered;

};

class SuggestionsController {
public:
	SuggestionsController(
		not_null<QWidget*> outer,
		not_null<QTextEdit*> field);

	void raise();
	void setReplaceCallback(Fn<void(
		int from,
		int till,
		const QString &replacement)> callback);

	static SuggestionsController *Init(
		not_null<QWidget*> outer,
		not_null<Ui::InputField*> field);

private:
	void handleCursorPositionChange();
	void handleTextChange();
	void showWithQuery(const QString &query);
	[[nodiscard]] QString getEmojiQuery();
	void suggestionsUpdated(bool visible);
	void updateGeometry();
	void updateForceHidden();
	void replaceCurrent(const QString &replacement);
	bool fieldFilter(not_null<QEvent*> event);
	bool outerFilter(not_null<QEvent*> event);

	bool _shown = false;
	bool _forceHidden = false;
	int _queryStartPosition = 0;
	bool _ignoreCursorPositionChange = false;
	bool _textChangeAfterKeyPress = false;
	QPointer<QTextEdit> _field;
	Fn<void(
		int from,
		int till,
		const QString &replacement)> _replaceCallback;
	base::unique_qptr<InnerDropdown> _container;
	QPointer<SuggestionsWidget> _suggestions;
	base::unique_qptr<QObject> _fieldFilter;
	base::unique_qptr<QObject> _outerFilter;
	base::Timer _showExactTimer;
	bool _keywordsRefreshed = false;
	QString _lastShownQuery;

	rpl::lifetime _lifetime;

};

} // namespace Emoji
} // namespace Ui
