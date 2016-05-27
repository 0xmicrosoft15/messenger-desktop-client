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
#include "stdafx.h"
#include "profile/profile_fixed_bar.h"

#include "styles/style_profile.h"
#include "lang.h"
#include "mainwidget.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"

namespace Profile {

class BackButton final : public Button {
public:
	BackButton(QWidget *parent) : Button(parent) {
		setCursor(style::cur_pointer);
	}

	void resizeToWidth(int newWidth) {
		resize(newWidth, st::profileTopBarHeight);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);

		p.fillRect(e->rect(), st::profileBg);
		st::profileTopBarBackIcon.paint(p, st::profileTopBarBackIconPosition, width());

		p.setFont(st::profileTopBarBackFont);
		p.setPen(st::profileTopBarBackFg);
		p.drawTextLeft(st::profileTopBarBackPosition.x(), st::profileTopBarBackPosition.y(), width(), lang(lng_menu_back));
	}
	void onStateChanged(int oldState, ButtonStateChangeSource source) override {
		if ((_state & Button::StateDown) && !(oldState & Button::StateDown)) {
			emit clicked();
		}
	}

private:

};

FixedBar::FixedBar(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _peerUser(peer->asUser())
, _peerChat(peer->asChat())
, _peerChannel(peer->asChannel())
, _peerMegagroup(peer->isMegagroup() ? _peerChannel : nullptr)
, _backButton(this) {
	_backButton->moveToLeft(0, 0);
	connect(_backButton, SIGNAL(clicked()), this, SLOT(onBack()));

	refreshRightActions();
}

void FixedBar::refreshRightActions() {
	_currentAction = 0;
	if (_peerUser) {
		setUserActions();
	} else if (_peerChat) {
		setChatActions();
	} else if (_peerMegagroup) {
		setMegagroupActions();
	} else if (_peerChannel) {
		setChannelActions();
	}
	while (_rightActions.size() > _currentAction) {
		delete _rightActions.back().button;
		_rightActions.pop_back();
	}
	resizeToWidth(width());
}

void FixedBar::setUserActions() {
	if (_peerUser->contact > 0) {
		addRightAction(RightActionType::EditContact, lang(lng_profile_edit_contact), SLOT(onEditContact()));
		addRightAction(RightActionType::DeleteContact, lang(lng_profile_delete_contact), SLOT(onDeleteContact()));
		addRightAction(RightActionType::ShareContact, lang(lng_profile_top_bar_share_contact), SLOT(onShareContact()));
	} else if (_peerUser->contact == 0 || !App::phoneFromSharedContact(peerToUser(_peer->id)).isEmpty()) {
		addRightAction(RightActionType::AddContact, lang(lng_profile_add_contact), SLOT(onAddContact()));
		addRightAction(RightActionType::ShareContact, lang(lng_profile_top_bar_share_contact), SLOT(onShareContact()));
	}
}

void FixedBar::setChatActions() {
	if (_peerChat->canEdit()) {
		addRightAction(RightActionType::EditGroup, lang(lng_profile_edit_contact), SLOT(onEditGroup()));
	}
	addRightAction(RightActionType::LeaveGroup, lang(lng_profile_delete_and_exit), SLOT(onLeaveGroup()));
}

void FixedBar::setMegagroupActions() {
	if (_peerMegagroup->amCreator() || _peerMegagroup->amEditor()) {
		addRightAction(RightActionType::EditChannel, lang(lng_profile_edit_contact), SLOT(onEditChannel()));
	}
}

void FixedBar::setChannelActions() {
	if (_peerChannel->amCreator()) {
		addRightAction(RightActionType::EditChannel, lang(lng_profile_edit_contact), SLOT(onEditChannel()));
	}
}

void FixedBar::addRightAction(RightActionType type, const QString &text, const char *slot) {
	if (_rightActions.size() > _currentAction) {
		if (_rightActions.at(_currentAction).type == type) {
			++_currentAction;
			return;
		}
	} else {
		t_assert(_rightActions.size() == _currentAction);
		_rightActions.push_back({});
	}
	_rightActions[_currentAction].type = type;
	_rightActions[_currentAction].button = new FlatButton(this, text, st::profileFixedBarButton);
	connect(_rightActions[_currentAction].button, SIGNAL(clicked()), this, slot);
	bool showButton = !_animatingMode && (type != RightActionType::ShareContact || !_hideShareContactButton);
	_rightActions[_currentAction].button->setVisible(showButton);
	++_currentAction;
}

void FixedBar::onBack() {
	App::main()->showBackFromStack();
}

void FixedBar::onEditChannel() {
	Ui::showLayer(new EditChannelBox(_peerMegagroup ? _peerMegagroup : _peerChannel));
}

void FixedBar::onEditGroup() {
	Ui::showLayer(new EditNameTitleBox(_peerChat));
}

void FixedBar::onAddContact() {
	auto firstName = _peerUser->firstName;
	auto lastName = _peerUser->lastName;
	auto phone = _peerUser->phone.isEmpty() ? App::phoneFromSharedContact(peerToUser(_peer->id)) : _peerUser->phone;
	Ui::showLayer(new AddContactBox(firstName, lastName, phone));
}

void FixedBar::onEditContact() {
	Ui::showLayer(new AddContactBox(_peerUser));
}

void FixedBar::onShareContact() {
	App::main()->shareContactLayer(_peerUser);
}

void FixedBar::onDeleteContact() {
	ConfirmBox *box = new ConfirmBox(lng_sure_delete_contact(lt_contact, App::peerName(_peerUser)), lang(lng_box_delete));
	connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteContactSure()));
	Ui::showLayer(box);
}

void FixedBar::onDeleteContactSure() {
	Ui::showChatsList();
	Ui::hideLayer();
	MTP::send(MTPcontacts_DeleteContact(_peerUser->inputUser), App::main()->rpcDone(&MainWidget::deletedContact, _peerUser));
}

void FixedBar::onLeaveGroup() {
	ConfirmBox *box = new ConfirmBox(lng_sure_delete_and_exit(lt_group, App::peerName(_peerChat)), lang(lng_box_leave), st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onLeaveGroupSure()));
	Ui::showLayer(box);
}

void FixedBar::onLeaveGroupSure() {
	Ui::showChatsList();
	Ui::hideLayer();
	MTP::send(MTPmessages_DeleteChatUser(_peerChat->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, _peer), App::main()->rpcFail(&MainWidget::leaveChatFailed, _peer));
}

void FixedBar::resizeToWidth(int newWidth) {
	int newHeight = 0;

	int buttonLeft = newWidth;
	for (auto i = _rightActions.cend(), b = _rightActions.cbegin(); i != b;) {
		--i;
		buttonLeft -= i->button->width();
		i->button->moveToLeft(buttonLeft, 0);
	}

	_backButton->moveToLeft(0, 0);
	_backButton->resizeToWidth(newWidth);
	newHeight += _backButton->height();

	resize(newWidth, newHeight);
}

void FixedBar::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setCursor(_animatingMode ? style::cur_pointer : style::cur_default);
		if (_animatingMode) {
			setAttribute(Qt::WA_OpaquePaintEvent, false);
			hideChildren();
		} else {
			setAttribute(Qt::WA_OpaquePaintEvent);
			showChildren();
			if (_hideShareContactButton) {
				applyHideShareContactButton();
			}
		}
		show();
	}
}

void FixedBar::setHideShareContactButton(bool hideButton) {
	_hideShareContactButton = hideButton;
	if (!_animatingMode) {
		applyHideShareContactButton();
	}
}

void FixedBar::applyHideShareContactButton() {
	for_const (auto &action, _rightActions) {
		if (action.type == RightActionType::ShareContact) {
			action.button->setVisible(_hideShareContactButton);
		}
	}
}

void FixedBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		onBack();
	} else {
		TWidget::mousePressEvent(e);
	}
}

} // namespace Profile
