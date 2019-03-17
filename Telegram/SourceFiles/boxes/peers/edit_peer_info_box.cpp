/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_info_box.h"

#include "apiwrap.h"
#include "auth_session.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_type_box.h"
#include "boxes/peers/edit_peer_history_visibility_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/stickers_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "history/admin_log/history_admin_log_section.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "mtproto/sender.h"
#include "observer_peer.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "ui/rp_widget.h"
#include "ui/special_buttons.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include <rpl/flatten_latest.h>
#include <rpl/range.h>
#include "info/profile/info_profile_icon.h"

namespace {

Fn<QString()> ManagePeerTitle(not_null<PeerData*> peer) {
	return langFactory((peer->isChat() || peer->isMegagroup())
		? lng_manage_group_title
		: lng_manage_channel_title);
}

auto ToPositiveNumberString() {
	return rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});
}

auto ToPositiveNumberStringRestrictions() {
	return rpl::map([](int count) {
		return QString::number(count)
		+ QString("/")
		+ QString::number(int(Data::ListOfRestrictions().size()));
	});
}

void AddSkip(not_null<Ui::VerticalLayout*> container) {
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerSkip));
	container->add(object_ptr<BoxContentDivider>(container));
}

void AddButtonWithCount(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::icon &icon) {
	EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		std::move(count),
		std::move(callback),
		st::manageGroupButton,
		&icon);
}

Info::Profile::Button *AddButtonWithText(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&label,
		Fn<void()> callback) {
	return EditPeerInfoBox::CreateButton(
		parent,
		std::move(text),
		std::move(label),
		std::move(callback),
		st::manageGroupTopButtonWithText,
		nullptr);
}

bool HasRecentActions(not_null<ChannelData*> channel) {
	return channel->hasAdminRights() || channel->amCreator();
}

void ShowRecentActions(
		not_null<Window::Navigation*> navigation,
		not_null<ChannelData*> channel) {
	navigation->showSection(AdminLog::SectionMemento(channel));
}

bool HasEditInfoBox(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		if (chat->canEditInformation()) {
			return true;
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->canEditInformation()) {
			return true;
		} else if (!channel->isPublic() && channel->canAddMembers()) {
			// Edit invite link.
			return true;
		}
	}
	return false;
}

void ShowEditPermissions(not_null<PeerData*> peer) {
	const auto box = Ui::show(
		Box<EditPeerPermissionsBox>(peer),
		LayerOption::KeepOther);
	box->saveEvents(
	) | rpl::start_with_next([=](MTPDchatBannedRights::Flags restrictions) {
		const auto callback = crl::guard(box, [=](bool success) {
			if (success) {
				box->closeBox();
			}
		});
		peer->session().api().saveDefaultRestrictions(
			peer->migrateToOrMe(),
			MTP_chatBannedRights(MTP_flags(restrictions), MTP_int(0)),
			callback);
	}, box->lifetime());
}

void FillManageSection(
		not_null<Window::Navigation*> navigation,
		not_null<PeerData*> peer,
		not_null<Ui::VerticalLayout*> content) {
	const auto chat = peer->asChat();
	const auto channel = peer->asChannel();
	const auto isChannel = (!chat);
	if (!chat && !channel) return;

	const auto canEditPermissions = [=] {
		return isChannel
			? channel->canEditPermissions()
			: chat->canEditPermissions();
	}();
	const auto canViewAdmins = [=] {
		return isChannel
			? channel->canViewAdmins()
			: chat->amIn();
	}();
	const auto canViewMembers = [=] {
		return isChannel
			? channel->canViewMembers()
			: chat->amIn();
	}();
	const auto canViewKicked = [=] {
		return isChannel
			? (!channel->isMegagroup())
			: false;
	}();
	const auto hasRecentActions = [=] {
		return isChannel
			? (channel->hasAdminRights() || channel->amCreator())
			: false;
	}();

	if (canEditPermissions) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_permissions),
			Info::Profile::RestrictionsCountValue(peer)
				| ToPositiveNumberStringRestrictions(),
			[=] { ShowEditPermissions(peer); },
			st::infoIconPermissions);
	}
	if (canViewAdmins) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_administrators),
			Info::Profile::AdminsCountValue(peer)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					peer,
					ParticipantsBoxController::Role::Admins);
			},
			st::infoIconAdministrators);
	}
	if (canViewMembers) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_members),
			Info::Profile::MembersCountValue(peer)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					peer,
					ParticipantsBoxController::Role::Members);
			},
			st::infoIconMembers);
	}
	if (canViewKicked) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_removed_users),
			Info::Profile::KickedCountValue(channel)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					peer,
					ParticipantsBoxController::Role::Kicked);
			},
			st::infoIconBlacklist);
	}
	if (hasRecentActions) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_recent_actions),
			rpl::single(QString()), //Empty count.
			[=] { ShowRecentActions(navigation, channel); },
			st::infoIconRecentActions);
	}
}

} // namespace

namespace {

constexpr auto kUsernameCheckTimeout = crl::time(200);
constexpr auto kMinUsernameLength = 5;
constexpr auto kMaxGroupChannelTitle = 255; // See also add_contact_box.
constexpr auto kMaxChannelDescription = 255; // See also add_contact_box.

class Controller
	: public base::has_weak_ptr
	, private MTP::Sender {
public:
	Controller(
		not_null<BoxContent*> box,
		not_null<PeerData*> peer);

	object_ptr<Ui::VerticalLayout> createContent();
	void setFocus();

private:
	struct Controls {
		Ui::InputField *title = nullptr;
		Ui::InputField *description = nullptr;
		Ui::UserpicButton *photo = nullptr;
		rpl::lifetime initialPhotoImageWaiting;

		std::shared_ptr<Ui::RadioenumGroup<Privacy>> privacy;
		Ui::SlideWrap<Ui::RpWidget> *usernameWrap = nullptr;
		Ui::UsernameInput *username = nullptr;
		base::unique_qptr<Ui::FlatLabel> usernameResult;
		const style::FlatLabel *usernameResultStyle = nullptr;

		Ui::SlideWrap<Ui::RpWidget> *createInviteLinkWrap = nullptr;
		Ui::SlideWrap<Ui::RpWidget> *editInviteLinkWrap = nullptr;
		Ui::FlatLabel *inviteLink = nullptr;

		Ui::SlideWrap<Ui::RpWidget> *historyVisibilityWrap = nullptr;
		std::optional<HistoryVisibility> historyVisibilitySavedValue = std::nullopt;
		std::optional<Privacy> privacySavedValue = std::nullopt;
		std::optional<QString> usernameSavedValue = std::nullopt;

		std::optional<bool> signaturesSavedValue = std::nullopt;
	};
	struct Saving {
		std::optional<QString> username;
		std::optional<QString> title;
		std::optional<QString> description;
		std::optional<bool> hiddenPreHistory;
		std::optional<bool> signatures;
	};

	Fn<QString()> computeTitle() const;
	object_ptr<Ui::RpWidget> createPhotoAndTitleEdit();
	object_ptr<Ui::RpWidget> createTitleEdit();
	object_ptr<Ui::RpWidget> createPhotoEdit();
	object_ptr<Ui::RpWidget> createDescriptionEdit();
	object_ptr<Ui::RpWidget> createUsernameEdit();
	object_ptr<Ui::RpWidget> createInviteLinkCreate();
	object_ptr<Ui::RpWidget> createInviteLinkEdit();
	object_ptr<Ui::RpWidget> createStickersEdit();
	object_ptr<Ui::RpWidget> createDeleteButton();

	object_ptr<Ui::RpWidget> createPrivaciesButtons();
	object_ptr<Ui::RpWidget> createManageGroupButtons();

	QString inviteLinkText() const;
	void observeInviteLink();

	void submitTitle();
	void submitDescription();
	void deleteWithConfirmation();
	void deleteChannel();

	std::optional<Saving> validate() const;
	bool validateUsername(Saving &to) const;
	bool validateTitle(Saving &to) const;
	bool validateDescription(Saving &to) const;
	bool validateHistoryVisibility(Saving &to) const;
	bool validateSignatures(Saving &to) const;

	void save();
	void saveUsername();
	void saveTitle();
	void saveDescription();
	void saveHistoryVisibility();
	void saveSignatures();
	void savePhoto();
	void pushSaveStage(FnMut<void()> &&lambda);
	void continueSave();
	void cancelSave();

	void subscribeToMigration();
	void migrate(not_null<ChannelData*> channel);

	not_null<BoxContent*> _box;
	not_null<PeerData*> _peer;
	bool _isGroup = false;

	base::unique_qptr<Ui::VerticalLayout> _wrap;
	Controls _controls;
	mtpRequestId _checkUsernameRequestId = 0;
	UsernameState _usernameState = UsernameState::Normal;
	rpl::event_stream<rpl::producer<QString>> _usernameResultTexts;

	std::deque<FnMut<void()>> _saveStagesQueue;
	Saving _savingData;

	rpl::lifetime _lifetime;

};

Controller::Controller(
	not_null<BoxContent*> box,
	not_null<PeerData*> peer)
: _box(box)
, _peer(peer)
, _isGroup(_peer->isChat() || _peer->isMegagroup()) {
	_box->setTitle(computeTitle());
	_box->addButton(langFactory(lng_settings_save), [this] {
		save();
	});
	_box->addButton(langFactory(lng_cancel), [this] {
		_box->closeBox();
	});
	subscribeToMigration();
	_peer->updateFull();
}

void Controller::subscribeToMigration() {
	SubscribeToMigration(
		_peer,
		_lifetime,
		[=](not_null<ChannelData*> channel) { migrate(channel); });
}

void Controller::migrate(not_null<ChannelData*> channel) {
	_peer = channel;
	// observeInviteLink();
	_peer->updateFull();
}

Fn<QString()> Controller::computeTitle() const {
	return langFactory(_isGroup
			? lng_edit_group
			: lng_edit_channel_title);
}

object_ptr<Ui::VerticalLayout> Controller::createContent() {
	auto result = object_ptr<Ui::VerticalLayout>(_box);
	_wrap.reset(result.data());
	_controls = Controls();

	_wrap->add(createPhotoAndTitleEdit());
	_wrap->add(createDescriptionEdit());

	AddSkip(_wrap); // Divider.
	_wrap->add(createPrivaciesButtons());
	AddSkip(_wrap); // Divider.
	_wrap->add(createManageGroupButtons());
	AddSkip(_wrap); // Divider.

	_wrap->add(createStickersEdit());
	_wrap->add(createDeleteButton());

	return result;
}

void Controller::setFocus() {
	if (_controls.title) {
		_controls.title->setFocusFast();
	}
}

object_ptr<Ui::RpWidget> Controller::createPhotoAndTitleEdit() {
	Expects(_wrap != nullptr);

	const auto canEdit = [&] {
		if (const auto channel = _peer->asChannel()) {
			return channel->canEditInformation();
		} else if (const auto chat = _peer->asChat()) {
			return chat->canEditInformation();
		}
		return false;
	}();
	if (!canEdit) {
		return nullptr;
	}

	auto result = object_ptr<Ui::RpWidget>(_wrap);
	auto container = result.data();

	auto photoWrap = Ui::AttachParentChild(
		container,
		createPhotoEdit());
	auto titleEdit = Ui::AttachParentChild(
		container,
		createTitleEdit());
	photoWrap->heightValue(
	) | rpl::start_with_next([container](int height) {
		container->resize(container->width(), height);
	}, photoWrap->lifetime());
	container->widthValue(
	) | rpl::start_with_next([titleEdit](int width) {
		auto left = st::editPeerPhotoMargins.left()
			+ st::defaultUserpicButton.size.width();
		titleEdit->resizeToWidth(width - left);
		titleEdit->moveToLeft(left, 0, width);
	}, titleEdit->lifetime());

	return result;
}

object_ptr<Ui::RpWidget> Controller::createPhotoEdit() {
	Expects(_wrap != nullptr);

	using PhotoWrap = Ui::PaddingWrap<Ui::UserpicButton>;
	auto photoWrap = object_ptr<PhotoWrap>(
		_wrap,
		object_ptr<Ui::UserpicButton>(
			_wrap,
			_peer,
			Ui::UserpicButton::Role::ChangePhoto,
			st::defaultUserpicButton),
		st::editPeerPhotoMargins);
	_controls.photo = photoWrap->entity();

	return photoWrap;
}

object_ptr<Ui::RpWidget> Controller::createTitleEdit() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::InputField>>(
		_wrap,
		object_ptr<Ui::InputField>(
			_wrap,
			st::defaultInputField,
			langFactory(_isGroup
				? lng_dlg_new_group_name
				: lng_dlg_new_channel_name),
			_peer->name),
		st::editPeerTitleMargins);
	result->entity()->setMaxLength(kMaxGroupChannelTitle);
	result->entity()->setInstantReplaces(Ui::InstantReplaces::Default());
	result->entity()->setInstantReplacesEnabled(
		Global::ReplaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		_wrap->window(),
		result->entity());

	QObject::connect(
		result->entity(),
		&Ui::InputField::submitted,
		[=] { submitTitle(); });

	_controls.title = result->entity();
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createDescriptionEdit() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::InputField>>(
		_wrap,
		object_ptr<Ui::InputField>(
			_wrap,
			st::editPeerDescription,
			Ui::InputField::Mode::MultiLine,
			langFactory(lng_create_group_description),
			_peer->about()),
		st::editPeerDescriptionMargins);
	result->entity()->setMaxLength(kMaxChannelDescription);
	result->entity()->setInstantReplaces(Ui::InstantReplaces::Default());
	result->entity()->setInstantReplacesEnabled(
		Global::ReplaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		_wrap->window(),
		result->entity());

	QObject::connect(
		result->entity(),
		&Ui::InputField::submitted,
		[=] { submitDescription(); });

	_controls.description = result->entity();
	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createPrivaciesButtons() {
	Expects(_wrap != nullptr);

	const auto canEditUsername = [&] {
		if (const auto chat = _peer->asChat()) {
			return chat->canEditUsername();
		} else if (const auto channel = _peer->asChannel()) {
			return channel->canEditUsername();
		}
		Unexpected("Peer type in Controller::createPrivaciesEdit.");
	}();
	if (!canEditUsername) {
		return nullptr;
	}

	const auto refreshHistoryVisibility = [=](bool instant = false) {
		if (!_controls.historyVisibilityWrap) {
			return;
		}
		_controls.historyVisibilityWrap->toggle(
			_controls.privacySavedValue == Privacy::Private,
			instant ? anim::type::instant : anim::type::normal);
	};

	const auto channel = _peer->asChannel();
	auto isRealChannel = !(!channel || !channel->canEditSignatures() || channel->isMegagroup());

	// Create Privacy Button.
	_controls.privacySavedValue = (_peer->isChannel()
		&& _peer->asChannel()->isPublic())
		? Privacy::Public
		: Privacy::Private;

	const auto updateType = std::make_shared<rpl::event_stream<Privacy>>();

	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerTopButtonsLayoutMargins);
	auto resultContainer = result->entity();

	const auto boxCallback = [=](Privacy checked, QString publicLink) {
		updateType->fire(std::move(checked));
		_controls.privacySavedValue = checked;
		_controls.usernameSavedValue = publicLink;
		refreshHistoryVisibility();
	};
	const auto buttonCallback = [=]{ 
		Ui::show(Box<EditPeerTypeBox>(
			_peer,
			boxCallback,
			_controls.privacySavedValue,
			_controls.usernameSavedValue
		), LayerOption::KeepOther);
	};
	AddButtonWithText(
		resultContainer,
		std::move(Lang::Viewer((_peer->isChat() || _peer->isMegagroup())
			? lng_manage_peer_group_type
			: lng_manage_peer_channel_type)),

		updateType->events(
		) | rpl::map([](Privacy flag) {
			return lang(Privacy::Public == flag
						? lng_manage_public_peer_title
						: lng_manage_private_peer_title);
		}),
		buttonCallback);
	
	updateType->fire(std::move(_controls.privacySavedValue.value()));

	// Create Signatures Toggle Button.
	if (isRealChannel) {
		AddButtonWithText(
			resultContainer,
			std::move(Lang::Viewer(lng_edit_sign_messages)),
			rpl::single(QString()),
			[=] {}
		)->toggleOn(rpl::single(channel->addsSignature())
		)->toggledValue(
		) | rpl::start_with_next([=](bool toggled) {
			_controls.signaturesSavedValue = toggled;
		}, resultContainer->lifetime());

		return std::move(result);
	}

	// Create History Visibility Button.

	const auto addHistoryVisibilityButton = [=](LangKey privacyTextKey, Ui::VerticalLayout* container) {
		// Bug with defaultValue here.
		_controls.historyVisibilitySavedValue = (!channel || channel->hiddenPreHistory())
			? HistoryVisibility::Hidden
			: HistoryVisibility::Visible;

		const auto updateHistoryVisibility = std::make_shared<rpl::event_stream<HistoryVisibility>>();

		const auto boxCallback = [=](HistoryVisibility checked) {
			updateHistoryVisibility->fire(std::move(checked));
			_controls.historyVisibilitySavedValue = checked;
		};
		const auto buttonCallback = [=]{ 
			Ui::show(Box<EditPeerHistoryVisibilityBox>(
				_peer,
				boxCallback,
				_controls.historyVisibilitySavedValue
			), LayerOption::KeepOther);
		};
		AddButtonWithText(
			container,
			std::move(Lang::Viewer(privacyTextKey)),
			updateHistoryVisibility->events(
			) | rpl::map([](HistoryVisibility flag) {
				return lang(HistoryVisibility::Visible == flag
						? lng_manage_history_visibility_shown
						: lng_manage_history_visibility_hidden);
			}),
			buttonCallback);

		updateHistoryVisibility->fire(
			std::move(_controls.historyVisibilitySavedValue.value())
		);
	};

	auto wrapLayout = resultContainer->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		resultContainer,
		object_ptr<Ui::VerticalLayout>(resultContainer),
		st::boxOptionListPadding)); // Empty margins.
	_controls.historyVisibilityWrap = wrapLayout;

	addHistoryVisibilityButton(lng_manage_history_visibility_title, wrapLayout->entity());

	//While appearing box we should use instant animation.
	refreshHistoryVisibility(true);

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createManageGroupButtons() {
	Expects(_wrap != nullptr);

	auto result = object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerBottomButtonsLayoutMargins);
	auto container = result->entity();

	FillManageSection(App::wnd()->controller(), _peer, container);
	// setDimensionsToContent(st::boxWidth, content);

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createStickersEdit() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel || !channel->canEditStickers()) {
		return nullptr;
	}

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap),
		st::editPeerInviteLinkMargins);
	auto container = result->entity();

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_group_stickers),
		st::editPeerSectionLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	container->add(object_ptr<Ui::FlatLabel>(
		container,
		Lang::Viewer(lng_group_stickers_description),
		st::editPeerPrivacyLabel));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::editPeerInviteLinkSkip));

	container->add(object_ptr<Ui::LinkButton>(
		_wrap,
		lang(lng_group_stickers_add),
		st::editPeerInviteLinkButton)
	)->addClickHandler([=] {
		Ui::show(Box<StickersBox>(channel), LayerOption::KeepOther);
	});

	return std::move(result);
}

object_ptr<Ui::RpWidget> Controller::createDeleteButton() {
	Expects(_wrap != nullptr);

	auto channel = _peer->asChannel();
	if (!channel || !channel->canDelete()) {
		return nullptr;
	}
	auto text = lang(_isGroup
		? lng_profile_delete_group
		: lng_profile_delete_channel);
	auto result = object_ptr<Ui::PaddingWrap<Ui::LinkButton>>(
		_wrap,
		object_ptr<Ui::LinkButton>(
			_wrap,
			text,
			st::editPeerDeleteButton),
		st::editPeerDeleteButtonMargins);
	result->entity()->addClickHandler([this] {
		deleteWithConfirmation();
	});
	return std::move(result);
}

void Controller::submitTitle() {
	Expects(_controls.title != nullptr);

	if (_controls.title->getLastText().isEmpty()) {
		_controls.title->showError();
		_box->scrollToWidget(_controls.title);
	} else if (_controls.description) {
		_controls.description->setFocus();
		_box->scrollToWidget(_controls.description);
	}
}

void Controller::submitDescription() {
	Expects(_controls.title != nullptr);
	Expects(_controls.description != nullptr);

	if (_controls.title->getLastText().isEmpty()) {
		_controls.title->showError();
		_box->scrollToWidget(_controls.title);
	} else {
		save();
	}
}

std::optional<Controller::Saving> Controller::validate() const {
	auto result = Saving();
	if (validateUsername(result)
		&& validateTitle(result)
		&& validateDescription(result)
		&& validateHistoryVisibility(result)
		&& validateSignatures(result)) {
		return result;
	}
	return {};
}

bool Controller::validateUsername(Saving &to) const {
	if (_controls.privacySavedValue != Privacy::Public) {
		to.username = QString();
		return true;
	}
	auto username = _controls.usernameSavedValue.value_or(
		_peer->isChannel()
			? _peer->asChannel()->username
			: QString()
	);
	if (username.isEmpty()) {
		return false;
	}
	to.username = username;
	return true;
}

bool Controller::validateTitle(Saving &to) const {
	if (!_controls.title) {
		return true;
	}
	auto title = _controls.title->getLastText().trimmed();
	if (title.isEmpty()) {
		_controls.title->showError();
		_box->scrollToWidget(_controls.title);
		return false;
	}
	to.title = title;
	return true;
}

bool Controller::validateDescription(Saving &to) const {
	if (!_controls.description) {
		return true;
	}
	to.description = _controls.description->getLastText().trimmed();
	return true;
}

bool Controller::validateHistoryVisibility(Saving &to) const {
	if (!_controls.historyVisibilityWrap) return true;
	
	if (!_controls.historyVisibilityWrap->toggled()
		|| (_controls.privacySavedValue == Privacy::Public)) {
		return true;
	}
	to.hiddenPreHistory
		= (_controls.historyVisibilitySavedValue == HistoryVisibility::Hidden);
	return true;
}

bool Controller::validateSignatures(Saving &to) const {
	if (!_controls.signaturesSavedValue.has_value()) {
		return true;
	}
	to.signatures = _controls.signaturesSavedValue;
	return true;
}

void Controller::save() {
	Expects(_wrap != nullptr);

	if (!_saveStagesQueue.empty()) {
		return;
	}
	if (auto saving = validate()) {
		_savingData = *saving;
		pushSaveStage([this] { saveUsername(); });
		pushSaveStage([this] { saveTitle(); });
		pushSaveStage([this] { saveDescription(); });
		pushSaveStage([this] { saveHistoryVisibility(); });
		pushSaveStage([this] { saveSignatures(); });
		pushSaveStage([this] { savePhoto(); });
		continueSave();
	}
}

void Controller::pushSaveStage(FnMut<void()> &&lambda) {
	_saveStagesQueue.push_back(std::move(lambda));
}

void Controller::continueSave() {
	if (!_saveStagesQueue.empty()) {
		auto next = std::move(_saveStagesQueue.front());
		_saveStagesQueue.pop_front();
		next();
	}
}

void Controller::cancelSave() {
	_saveStagesQueue.clear();
}

void Controller::saveUsername() {
	const auto channel = _peer->asChannel();
	const auto username = (channel ? channel->username : QString());
	if (!_savingData.username || *_savingData.username == username) {
		return continueSave();
	} else if (!channel) {
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			if (_peer->asChannel() == channel) {
				saveUsername();
			} else {
				cancelSave();
			}
		};
		_peer->session().api().migrateChat(
			_peer->asChat(),
			crl::guard(this, saveForChannel));
		return;
	}

	request(MTPchannels_UpdateUsername(
		channel->inputChannel,
		MTP_string(*_savingData.username)
	)).done([=](const MTPBool &result) {
		channel->setName(
			TextUtilities::SingleLine(channel->name),
			*_savingData.username);
		continueSave();
	}).fail([=](const RPCError &error) {
		const auto &type = error.type();
		if (type == qstr("USERNAME_NOT_MODIFIED")) {
			channel->setName(
				TextUtilities::SingleLine(channel->name),
				TextUtilities::SingleLine(*_savingData.username));
			continueSave();
			return;
		}
		const auto errorKey = [&] {
			if (type == qstr("USERNAME_INVALID")) {
				return lng_create_channel_link_invalid;
			} else if (type == qstr("USERNAME_OCCUPIED")
				|| type == qstr("USERNAMES_UNAVAILABLE")) {
				return lng_create_channel_link_invalid;
			}
			return lng_create_channel_link_invalid;
		}();
		_controls.username->showError();
		_box->scrollToWidget(_controls.username);
		// showUsernameError(Lang::Viewer(errorKey));
		cancelSave();
	}).send();
}

void Controller::saveTitle() {
	if (!_savingData.title || *_savingData.title == _peer->name) {
		return continueSave();
	}

	const auto onDone = [=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		continueSave();
	};
	const auto onFail = [=](const RPCError &error) {
		const auto &type = error.type();
		if (type == qstr("CHAT_NOT_MODIFIED")
			|| type == qstr("CHAT_TITLE_NOT_MODIFIED")) {
			if (const auto channel = _peer->asChannel()) {
				channel->setName(*_savingData.title, channel->username);
			} else if (const auto chat = _peer->asChat()) {
				chat->setName(*_savingData.title);
			}
			continueSave();
			return;
		}
		_controls.title->showError();
		if (type == qstr("NO_CHAT_TITLE")) {
			_box->scrollToWidget(_controls.title);
		}
		cancelSave();
	};

	if (const auto channel = _peer->asChannel()) {
		request(MTPchannels_EditTitle(
			channel->inputChannel,
			MTP_string(*_savingData.title)
		)).done(std::move(onDone)
		).fail(std::move(onFail)
		).send();
	} else if (const auto chat = _peer->asChat()) {
		request(MTPmessages_EditChatTitle(
			chat->inputChat,
			MTP_string(*_savingData.title)
		)).done(std::move(onDone)
		).fail(std::move(onFail)
		).send();
	} else {
		continueSave();
	}
}

void Controller::saveDescription() {
	const auto channel = _peer->asChannel();
	if (!_savingData.description
		|| *_savingData.description == _peer->about()) {
		return continueSave();
	}
	const auto successCallback = [=] {
		_peer->setAbout(*_savingData.description);
		continueSave();
	};
	request(MTPmessages_EditChatAbout(
		_peer->input,
		MTP_string(*_savingData.description)
	)).done([=](const MTPBool &result) {
		successCallback();
	}).fail([=](const RPCError &error) {
		const auto &type = error.type();
		if (type == qstr("CHAT_ABOUT_NOT_MODIFIED")) {
			successCallback();
			return;
		}
		_controls.description->showError();
		cancelSave();
	}).send();
}

void Controller::saveHistoryVisibility() {
	const auto channel = _peer->asChannel();
	const auto hidden = channel ? channel->hiddenPreHistory() : true;
	if (!_savingData.hiddenPreHistory
		|| *_savingData.hiddenPreHistory == hidden) {
		return continueSave();
	} else if (!channel) {
		const auto saveForChannel = [=](not_null<ChannelData*> channel) {
			if (_peer->asChannel() == channel) {
				saveHistoryVisibility();
			} else {
				cancelSave();
			}
		};
		_peer->session().api().migrateChat(
			_peer->asChat(),
			crl::guard(this, saveForChannel));
		return;
	}
	request(MTPchannels_TogglePreHistoryHidden(
		channel->inputChannel,
		MTP_bool(*_savingData.hiddenPreHistory)
	)).done([=](const MTPUpdates &result) {
		// Update in the result doesn't contain the
		// channelFull:flags field which holds this value.
		// So after saving we need to update it manually.
		channel->updateFullForced();

		channel->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const RPCError &error) {
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::saveSignatures() {
	const auto channel = _peer->asChannel();
	if (!_savingData.signatures
		|| !channel
		|| *_savingData.signatures == channel->addsSignature()) {
		return continueSave();
	}
	request(MTPchannels_ToggleSignatures(
		channel->inputChannel,
		MTP_bool(*_savingData.signatures)
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		continueSave();
	}).fail([=](const RPCError &error) {
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
			continueSave();
		} else {
			cancelSave();
		}
	}).send();
}

void Controller::savePhoto() {
	auto image = _controls.photo
		? _controls.photo->takeResultImage()
		: QImage();
	if (!image.isNull()) {
		_peer->session().api().uploadPeerPhoto(_peer, std::move(image));
	}
	_box->closeBox();
}

void Controller::deleteWithConfirmation() {
	const auto channel = _peer->asChannel();
	Assert(channel != nullptr);

	const auto text = lang(_isGroup
		? lng_sure_delete_group
		: lng_sure_delete_channel);
	const auto deleteCallback = crl::guard(this, [=] {
		deleteChannel();
	});
	Ui::show(
		Box<ConfirmBox>(
			text,
			lang(lng_box_delete),
			st::attentionBoxButton,
			deleteCallback),
		LayerOption::KeepOther);
}

void Controller::deleteChannel() {
	const auto channel = _peer->asChannel();
	Assert(channel != nullptr);

	const auto chat = channel->migrateFrom();

	Ui::hideLayer();
	Ui::showChatsList();
	if (chat) {
		App::main()->deleteAndExit(chat);
	}
	MTP::send(
		MTPchannels_DeleteChannel(channel->inputChannel),
		App::main()->rpcDone(&MainWidget::sentUpdatesReceived),
		App::main()->rpcFail(&MainWidget::deleteChannelFailed));
}

} // namespace

EditPeerInfoBox::EditPeerInfoBox(
	QWidget*,
	not_null<PeerData*> peer)
: _peer(peer->migrateToOrMe()) {
}

void EditPeerInfoBox::prepare() {
	auto controller = Ui::CreateChild<Controller>(this, this, _peer);
	_focusRequests.events(
	) | rpl::start_with_next(
		[=] { controller->setFocus(); },
		lifetime());
	auto content = controller->createContent();
	setDimensionsToContent(st::boxWideWidth, content);
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(content)));
}

Info::Profile::Button *EditPeerInfoBox::CreateButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::InfoProfileCountButton &st,
		const style::icon *icon) {
	const auto button = parent->add(
		object_ptr<Info::Profile::Button>(
			parent,
			std::move(text),
			st.button));
	button->addClickHandler(callback);
	if (icon) {
		Ui::CreateChild<Info::Profile::FloatingIcon>(
			button,
			*icon,
			st.iconPosition);
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		std::move(count),
		st.label);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	rpl::combine(
		button->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=, &st](int outerWidth, int width) {
		label->moveToRight(
			st.labelPosition.x(),
			st.labelPosition.y(),
			outerWidth);
	}, label->lifetime());

	return button;
}

bool EditPeerInfoBox::Available(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return false
			|| chat->canEditInformation()
			|| chat->canEditPermissions();
	} else if (const auto channel = peer->asChannel()) {
		// canViewMembers() is removed, because in supergroups you
		// see them in profile and in channels only admins can see them.

		// canViewAdmins() is removed, because in supergroups it is
		// always true and in channels it is equal to canViewBanned().

		return false
			//|| channel->canViewMembers()
			//|| channel->canViewAdmins()
			|| channel->canViewBanned()
			|| channel->canEditInformation()
			|| channel->canEditPermissions()
			|| HasRecentActions(channel);
	} else {
		return false;
	}
}