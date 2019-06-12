/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_contact_status.h"

#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "boxes/confirm_box.h"
#include "boxes/generic_box.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"

namespace HistoryView {
namespace {

bool BarCurrentlyHidden(not_null<PeerData*> peer) {
	const auto settings = peer->settings();
	if (!settings) {
		return false;
	} else if (!(*settings)) {
		return true;
	}
	using Setting = MTPDpeerSettings::Flag;
	if (const auto user = peer->asUser()) {
		if (user->isBlocked()) {
			return true;
		} else if (user->isContact()
			&& !((*settings) & Setting::f_share_contact)) {
			return true;
		}
	} else if (!((*settings) & Setting::f_report_spam)) {
		return true;
	}
	return false;
}

auto MapToEmpty() {
	return rpl::map([] { return rpl::empty_value(); });
}

} // namespace

ContactStatus::Bar::Bar(QWidget *parent, const QString &name)
: RpWidget(parent)
, _name(name)
, _add(
	this,
	QString(),
	st::historyContactStatusButton)
, _block(
	this,
	lang(lng_new_contact_block).toUpper(),
	st::historyContactStatusBlock)
, _share(
	this,
	lang(lng_new_contact_share).toUpper(),
	st::historyContactStatusButton)
, _report(
	this,
	lang(lng_report_spam_and_leave).toUpper(),
	st::historyContactStatusBlock)
, _close(this, st::historyReplyCancel) {
	resize(_close->size());
}

void ContactStatus::Bar::showState(State state) {
	_add->setVisible(state == State::AddOrBlock || state == State::Add);
	_block->setVisible(state == State::AddOrBlock);
	_share->setVisible(state == State::SharePhoneNumber);
	_report->setVisible(state == State::ReportSpam);
	_add->setText((state == State::Add)
		? lng_new_contact_add_name(lt_user, _name).toUpper()
		: lang(lng_new_contact_add).toUpper());
	updateButtonsGeometry();
}

rpl::producer<> ContactStatus::Bar::addClicks() const {
	return _add->clicks() | MapToEmpty();
}

rpl::producer<> ContactStatus::Bar::blockClicks() const {
	return _block->clicks() | MapToEmpty();
}

rpl::producer<> ContactStatus::Bar::shareClicks() const {
	return _share->clicks() | MapToEmpty();
}

rpl::producer<> ContactStatus::Bar::reportClicks() const {
	return _report->clicks() | MapToEmpty();
}

rpl::producer<> ContactStatus::Bar::closeClicks() const {
	return _close->clicks() | MapToEmpty();
}

void ContactStatus::Bar::resizeEvent(QResizeEvent *e) {
	_close->moveToRight(0, 0);
	updateButtonsGeometry();
}

void ContactStatus::Bar::updateButtonsGeometry() {
	const auto full = width();
	const auto closeWidth = _close->width();
	const auto available = full - closeWidth;
	const auto skip = st::historyContactStatusMinSkip;
	const auto buttonWidth = [&](const object_ptr<Ui::FlatButton> &button) {
		return button->textWidth() + 2 * skip;
	};

	auto accumulatedLeft = 0;
	const auto placeButton = [&](
			const object_ptr<Ui::FlatButton> &button,
			int buttonWidth,
			int rightTextMargin = 0) {
		button->setGeometry(accumulatedLeft, 0, buttonWidth, height());
		button->setTextMargins({ 0, 0, rightTextMargin, 0 });
		accumulatedLeft += buttonWidth;
	};
	const auto placeOne = [&](const object_ptr<Ui::FlatButton> &button) {
		if (button->isHidden()) {
			return;
		}
		const auto thatWidth = buttonWidth(button);
		const auto margin = std::clamp(
			thatWidth + closeWidth - available,
			0,
			closeWidth);
		placeButton(button, full, margin);
	};
	if (!_add->isHidden() && !_block->isHidden()) {
		const auto addWidth = buttonWidth(_add);
		const auto blockWidth = buttonWidth(_block);
		const auto half = full / 2;
		if (addWidth <= half
			&& blockWidth + 2 * closeWidth <= full - half) {
			placeButton(_add, half);
			placeButton(_block, full - half);
		} else if (addWidth + blockWidth <= available) {
			const auto margin = std::clamp(
				addWidth + blockWidth + closeWidth - available,
				0,
				closeWidth);
			const auto realBlockWidth = blockWidth + 2 * closeWidth - margin;
			if (addWidth > realBlockWidth) {
				placeButton(_add, addWidth);
				placeButton(_block, full - addWidth, margin);
			} else {
				placeButton(_add, full - realBlockWidth);
				placeButton(_block, realBlockWidth, margin);
			}
		} else {
			const auto forAdd = (available * addWidth)
				/ (addWidth + blockWidth);
			placeButton(_add, forAdd);
			placeButton(_block, full - forAdd, closeWidth);
		}
	} else {
		placeOne(_add);
		placeOne(_share);
		placeOne(_report);
	}
}

ContactStatus::ContactStatus(
	not_null<Window::Controller*> window,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _window(window)
, _bar(parent, object_ptr<Bar>(parent, peer->shortName()))
, _shadow(parent) {
	setupWidgets(parent);
	setupState(peer);
	setupHandlers(peer);
}

void ContactStatus::setupWidgets(not_null<Ui::RpWidget*> parent) {
	parent->widthValue(
	) | rpl::start_with_next([=](int width) {
		_bar.resizeToWidth(width);
	}, _bar.lifetime());

	_bar.geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		_shadow.setGeometry(
			geometry.x(),
			geometry.y() + geometry.height(),
			geometry.width(),
			st::lineWidth);
	}, _shadow.lifetime());

	_bar.shownValue(
	) | rpl::start_with_next([=](bool shown) {
		_shadow.setVisible(shown);
	}, _shadow.lifetime());
}

auto ContactStatus::PeerState(not_null<PeerData*> peer)
-> rpl::producer<State> {
	using SettingsChange = PeerData::Settings::Change;
	using Setting = MTPDpeerSettings::Flag;
	if (const auto user = peer->asUser()) {
		using FlagsChange = UserData::Flags::Change;
		using FullFlagsChange = UserData::FullFlags::Change;
		using Flag = MTPDuser::Flag;
		using FullFlag = MTPDuserFull::Flag;

		auto isContactChanges = user->flagsValue(
		) | rpl::filter([](FlagsChange flags) {
			return flags.diff
				& (Flag::f_contact | Flag::f_mutual_contact);
		});
		auto isBlockedChanges = user->fullFlagsValue(
		) | rpl::filter([](FullFlagsChange full) {
			return full.diff & FullFlag::f_blocked;
		});
		return rpl::combine(
			std::move(isContactChanges),
			std::move(isBlockedChanges),
			user->settingsValue()
		) | rpl::map([=](
				FlagsChange flags,
				FullFlagsChange full,
				SettingsChange settings) {
			if (!settings.value || (full.value & FullFlag::f_blocked)) {
				return State::None;
			} else if (user->isContact()) {
				if (settings.value & Setting::f_share_contact) {
					return State::SharePhoneNumber;
				} else {
					return State::None;
				}
			} else if (settings.value & Setting::f_block_contact) {
				return State::AddOrBlock;
			} else {
				return State::Add;
			}
		});
	}

	return peer->settingsValue(
	) | rpl::map([=](SettingsChange settings) {
		return (settings.value & Setting::f_report_spam)
			? State::ReportSpam
			: State::None;
	});
}

void ContactStatus::setupState(not_null<PeerData*> peer) {
	if (!BarCurrentlyHidden(peer)) {
		peer->session().api().requestPeerSettings(peer);
	}

	PeerState(
		peer
	) | rpl::start_with_next([=](State state) {
		_state = state;
		if (state == State::None) {
			_bar.hide(anim::type::normal);
		} else {
			_bar.entity()->showState(state);
			_bar.show(anim::type::normal);
		}
	}, _bar.lifetime());
}

void ContactStatus::setupHandlers(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		setupAddHandler(user);
		setupBlockHandler(user);
		setupShareHandler(user);
	}
	setupReportHandler(peer);
	setupCloseHandler(peer);
}

void ContactStatus::setupAddHandler(not_null<UserData*> user) {
	_bar.entity()->addClicks(
	) | rpl::start_with_next([=] {
		Window::PeerMenuAddContact(user);
	}, _bar.lifetime());
}

void ContactStatus::setupBlockHandler(not_null<UserData*> user) {
	_bar.entity()->blockClicks(
	) | rpl::start_with_next([=] {
		_window->show(Box(Window::PeerMenuBlockUserBox, user, _window));
	}, _bar.lifetime());
}

void ContactStatus::setupShareHandler(not_null<UserData*> user) {
	_bar.entity()->shareClicks(
	) | rpl::start_with_next([=] {
		user->setSettings(0);
		user->session().api().request(MTPcontacts_AcceptContact(
			user->inputUser
		)).done([=](const MTPUpdates &result) {
			user->session().api().applyUpdates(result);

			Ui::Toast::Show(
				lng_new_contact_share_done(lt_user, user->shortName()));
		}).send();
	}, _bar.lifetime());
}

void ContactStatus::setupReportHandler(not_null<PeerData*> peer) {
	_bar.entity()->reportClicks(
	) | rpl::start_with_next([=] {
		Expects(!peer->isUser());

		const auto box = std::make_shared<QPointer<BoxContent>>();
		const auto callback = crl::guard(&_bar, [=] {
			if (*box) {
				(*box)->closeBox();
			}

			peer->session().api().request(MTPmessages_ReportSpam(
				peer->input
			)).send();

			crl::on_main(&peer->session(), [=] {
				if (const auto from = peer->migrateFrom()) {
					peer->session().api().deleteConversation(from, false);
				}
				peer->session().api().deleteConversation(peer, false);
			});

			Ui::Toast::Show(lang(lng_report_spam_done));

			// Destroys _bar.
			_window->sessionController()->showBackFromStack();
		});
		if (const auto user = peer->asUser()) {
			peer->session().api().blockUser(user);
		}
		const auto text = lang((peer->isChat() || peer->isMegagroup())
				? lng_report_spam_sure_group
				: lng_report_spam_sure_channel);
		_window->show(Box<ConfirmBox>(
			text,
			lang(lng_report_spam_ok),
			st::attentionBoxButton,
			callback));
	}, _bar.lifetime());
}

void ContactStatus::setupCloseHandler(not_null<PeerData*> peer) {
	const auto request = _bar.lifetime().make_state<mtpRequestId>(0);
	_bar.entity()->closeClicks(
	) | rpl::filter([=] {
		return !(*request);
	}) | rpl::start_with_next([=] {
		peer->setSettings(0);
		*request = peer->session().api().request(
			MTPmessages_HidePeerSettingsBar(peer->input)
		).send();
	}, _bar.lifetime());
}

void ContactStatus::show() {
	const auto visible = (_state != State::None);
	if (!_shown) {
		_shown = true;
		if (visible) {
			_bar.entity()->showState(_state);
		}
	}
	_bar.toggle(visible, anim::type::instant);
}

void ContactStatus::raise() {
	_bar.raise();
	_shadow.raise();
}

void ContactStatus::move(int x, int y) {
	_bar.move(x, y);
	_shadow.move(x, y + _bar.height());
}

int ContactStatus::height() const {
	return _bar.height();
}

rpl::producer<int> ContactStatus::heightValue() const {
	return _bar.heightValue();
}

} // namespace HistoryView
