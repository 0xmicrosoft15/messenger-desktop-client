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

namespace Ui {
class IconButton;
class RoundButton;
class LinkButton;
class SlideAnimation;
class CrossFadeAnimation;
class FlatLabel;
template <typename Widget>
class WidgetFadeWrap;
} // namespace Ui

namespace Intro {

class Widget : public TWidget, public RPCSender {
	Q_OBJECT

public:
	Widget(QWidget *parent);

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	void setInnerFocus();

	~Widget();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

signals:
	void countryChanged();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
private slots:
	void onCheckUpdateStatus();
#endif // TDESKTOP_DISABLE_AUTOUPDATE

	// Internal interface.
public:
	struct Data {
		QString country;
		QString phone;
		QString phoneHash;
		bool phoneIsRegistered = false;

		enum class CallStatus {
			Waiting,
			Calling,
			Called,
			Disabled,
		};
		CallStatus callStatus = CallStatus::Disabled;
		int callTimeout = 0;

		QString code;
		int codeLength = 5;
		bool codeByTelegram = false;

		QByteArray pwdSalt;
		bool hasRecovery = false;
		QString pwdHint;

		base::Observable<void> updated;

	};

	enum class Direction {
		Back,
		Forward,
		Replace,
	};
	class Step : public TWidget, public RPCSender {
	public:
		Step(QWidget *parent, Data *data, bool hasCover = false);

		virtual void setInnerFocus() {
			setFocus();
		}

		void setGoCallback(base::lambda<void(Step *step, Direction direction)> &&callback);
		void setShowResetCallback(base::lambda<void()> &&callback);

		void prepareShowAnimated(Step *after);
		void showAnimated(Direction direction);
		void showFast();
		bool animating() const;

		bool hasCover() const;
		virtual bool hasBack() const;
		virtual void activate();
		virtual void cancelled();
		virtual void finished();

		virtual void submit() = 0;
		virtual QString nextButtonText() const;

		int contentLeft() const;
		int contentTop() const;

		void setErrorCentered(bool centered);
		void setErrorBelowLink(bool below);
		void showError(const QString &text);
		void hideError() {
			showError(QString());
		}

	protected:
		void paintEvent(QPaintEvent *e) override;
		void resizeEvent(QResizeEvent *e) override;

		void setTitleText(QString richText);
		void setDescriptionText(QString richText);
		bool paintAnimated(Painter &p, QRect clip);

		void fillSentCodeData(const MTPauth_SentCodeType &type);

		void showDescription();
		void hideDescription();

		Data *getData() const {
			return _data;
		}
		void finish(const MTPUser &user, QImage photo = QImage());

		void goBack() {
			if (_goCallback) _goCallback(nullptr, Direction::Back);
		}
		void goNext(Step *step) {
			if (_goCallback) _goCallback(step, Direction::Forward);
		}
		void goReplace(Step *step) {
			if (_goCallback) _goCallback(step, Direction::Replace);
		}
		void showResetButton() {
			if (_showResetCallback) _showResetCallback();
		}

	private:
		struct CoverAnimation {
			std_::unique_ptr<Ui::CrossFadeAnimation> title;
			std_::unique_ptr<Ui::CrossFadeAnimation> description;

			// From content top till the next button top.
			QPixmap contentSnapshotWas;
			QPixmap contentSnapshotNow;
		};
		void updateLabelsPosition();
		void paintContentSnapshot(Painter &p, const QPixmap &snapshot, float64 alpha, float64 howMuchHidden);

		CoverAnimation prepareCoverAnimation(Step *step);
		QPixmap prepareContentSnapshot();
		QPixmap prepareSlideAnimation();
		void showFinished();

		void prepareCoverMask();
		void paintCover(Painter &p, int top);

		Data *_data = nullptr;
		bool _hasCover = false;
		base::lambda<void(Step *step, Direction direction)> _goCallback;
		base::lambda<void()> _showResetCallback;

		ChildWidget<Ui::FlatLabel> _title;
		ChildWidget<Ui::WidgetFadeWrap<Ui::FlatLabel>> _description;

		bool _errorCentered = false;
		bool _errorBelowLink = false;
		QString _errorText;
		ChildWidget<Ui::WidgetFadeWrap<Ui::FlatLabel>> _error = { nullptr };

		FloatAnimation _a_show;
		CoverAnimation _coverAnimation;
		std_::unique_ptr<Ui::SlideAnimation> _slideAnimation;
		QPixmap _coverMask;

	};

private:
	void step_show(float64 ms, bool timer);

	void changeLanguage(int32 languageId);
	void updateControlsGeometry();
	Data *getData() {
		return &_data;
	}

	void fixOrder();
	void showControls();
	void hideControls();
	void moveControls();
	QRect calculateStepRect() const;

	void showResetButton();
	void resetAccount();
	void resetAccountSure();
	void resetDone(const MTPBool &result);
	bool resetFail(const RPCError &error);

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_shadow;

	QVector<Step*> _stepHistory;
	Step *getStep(int skip = 0) {
		t_assert(_stepHistory.size() + skip > 0);
		return _stepHistory.at(_stepHistory.size() - skip - 1);
	}
	void historyMove(Direction direction);
	void moveToStep(Step *step, Direction direction);
	void appendStep(Step *step);

	void gotNearestDC(const MTPNearestDc &dc);

	Data _data;

	FloatAnimation _coverShownAnimation;
	int _nextTopFrom = 0;
	int _controlsTopFrom = 0;

	ChildWidget<Ui::WidgetFadeWrap<Ui::IconButton>> _back;
	ChildWidget<Ui::WidgetFadeWrap<Ui::RoundButton>> _update = { nullptr };
	ChildWidget<Ui::WidgetFadeWrap<Ui::RoundButton>> _settings;

	ChildWidget<Ui::RoundButton> _next;
	ChildWidget<Ui::WidgetFadeWrap<Ui::LinkButton>> _changeLanguage = { nullptr };
	ChildWidget<Ui::WidgetFadeWrap<Ui::RoundButton>> _resetAccount = { nullptr };

	mtpRequestId _resetRequest = 0;

};

} // namespace Intro
