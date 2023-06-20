/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_top_bar.h"

#include <rpl/never.h>
#include <rpl/merge.h>
#include "dialogs/ui/dialogs_stories_content.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "lang/lang_keys.h"
#include "lang/lang_numbers_animation.h"
#include "info/info_wrap_widget.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "storage/storage_shared_media.h"
#include "boxes/delete_messages_box.h"
#include "boxes/peer_list_controllers.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "window/window_peer_menu.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"

namespace Info {

TopBar::TopBar(
	QWidget *parent,
	not_null<Window::SessionNavigation*> navigation,
	const style::InfoTopBar &st,
	SelectedItems &&selectedItems)
: RpWidget(parent)
, _navigation(navigation)
, _st(st)
, _selectedItems(Section::MediaType::kCount) {
	if (_st.radius) {
		_roundRect.emplace(_st.radius, _st.bg);
	}
	setAttribute(Qt::WA_OpaquePaintEvent, !_roundRect);
	setSelectedItems(std::move(selectedItems));
	updateControlsVisibility(anim::type::instant);
}

template <typename Callback>
void TopBar::registerUpdateControlCallback(
		QObject *guard,
		Callback &&callback) {
	_updateControlCallbacks[guard] =[
		weak = Ui::MakeWeak(guard),
		callback = std::forward<Callback>(callback)
	](anim::type animated) {
		if (!weak) {
			return false;
		}
		callback(animated);
		return true;
	};
}

template <typename Widget, typename IsVisible>
void TopBar::registerToggleControlCallback(
		Widget *widget,
		IsVisible &&callback) {
	registerUpdateControlCallback(widget, [
		widget,
		isVisible = std::forward<IsVisible>(callback)
	](anim::type animated) {
		widget->toggle(isVisible(), animated);
	});
}

void TopBar::setTitle(rpl::producer<QString> &&title) {
	if (_title) {
		delete _title;
	}
	_title = Ui::CreateChild<Ui::FadeWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(this, std::move(title), _st.title),
		st::infoTopBarScale);
	_title->setDuration(st::infoTopBarDuration);
	_title->toggle(
		!selectionMode() && !storiesTitle(),
		anim::type::instant);
	registerToggleControlCallback(_title.data(), [=] {
		return !selectionMode() && !storiesTitle() && !searchMode();
	});

	if (_back) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	updateControlsGeometry(width());
}

void TopBar::enableBackButton() {
	if (_back) {
		return;
	}
	_back = Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.back),
		st::infoTopBarScale);
	_back->setDuration(st::infoTopBarDuration);
	_back->toggle(!selectionMode(), anim::type::instant);
	_back->entity()->clicks(
	) | rpl::to_empty
	| rpl::start_to_stream(_backClicks, _back->lifetime());
	registerToggleControlCallback(_back.data(), [=] {
		return !selectionMode();
	});

	if (_title) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	updateControlsGeometry(width());
}

void TopBar::createSearchView(
		not_null<Ui::SearchFieldController*> controller,
		rpl::producer<bool> &&shown,
		bool startsFocused) {
	setSearchField(
		controller->createField(this, _st.searchRow.field),
		std::move(shown),
		startsFocused);
}

bool TopBar::focusSearchField() {
	if (_searchField && _searchField->isVisible()) {
		_searchField->setFocus();
		return true;
	}
	return false;
}

Ui::FadeWrap<Ui::RpWidget> *TopBar::pushButton(
		base::unique_qptr<Ui::RpWidget> button) {
	auto wrapped = base::make_unique_q<Ui::FadeWrap<Ui::RpWidget>>(
		this,
		object_ptr<Ui::RpWidget>::fromRaw(button.release()),
		st::infoTopBarScale);
	auto weak = wrapped.get();
	_buttons.push_back(std::move(wrapped));
	weak->setDuration(st::infoTopBarDuration);
	registerToggleControlCallback(weak, [=] {
		return !selectionMode()
			&& !_searchModeEnabled;
	});
	weak->toggle(
		!selectionMode() && !_searchModeEnabled,
		anim::type::instant);
	weak->widthValue(
	) | rpl::start_with_next([this] {
		updateControlsGeometry(width());
	}, lifetime());
	return weak;
}

void TopBar::forceButtonVisibility(
		Ui::FadeWrap<Ui::RpWidget> *button,
		rpl::producer<bool> shown) {
	_updateControlCallbacks.erase(button);
	button->toggleOn(std::move(shown));
}

void TopBar::setSearchField(
		base::unique_qptr<Ui::InputField> field,
		rpl::producer<bool> &&shown,
		bool startsFocused) {
	Expects(field != nullptr);
	createSearchView(field.release(), std::move(shown), startsFocused);
}

void TopBar::clearSearchField() {
	_searchView = nullptr;
}

void TopBar::createSearchView(
		not_null<Ui::InputField*> field,
		rpl::producer<bool> &&shown,
		bool startsFocused) {
	_searchView = base::make_unique_q<Ui::FixedHeightWidget>(
		this,
		_st.searchRow.height);
	auto wrap = _searchView.get();
	registerUpdateControlCallback(wrap, [=](anim::type) {
		wrap->setVisible(!selectionMode() && _searchModeAvailable);
	});

	_searchField = field;
	auto fieldWrap = Ui::CreateChild<Ui::FadeWrap<Ui::InputField>>(
		wrap,
		object_ptr<Ui::InputField>::fromRaw(field),
		st::infoTopBarScale);
	fieldWrap->setDuration(st::infoTopBarDuration);

	auto focusLifetime = field->lifetime().make_state<rpl::lifetime>();
	registerUpdateControlCallback(fieldWrap, [=](anim::type animated) {
		auto fieldShown = !selectionMode() && searchMode();
		if (!fieldShown && field->hasFocus()) {
			setFocus();
		}
		fieldWrap->toggle(fieldShown, animated);
		if (fieldShown) {
			*focusLifetime = field->shownValue()
				| rpl::filter([](bool shown) { return shown; })
				| rpl::take(1)
				| rpl::start_with_next([=] { field->setFocus(); });
		} else {
			focusLifetime->destroy();
		}
	});

	auto button = base::make_unique_q<Ui::IconButton>(this, _st.search);
	auto search = button.get();
	search->addClickHandler([=] { showSearch(); });
	auto searchWrap = pushButton(std::move(button));
	registerToggleControlCallback(searchWrap, [=] {
		return !selectionMode()
			&& _searchModeAvailable
			&& !_searchModeEnabled;
	});

	auto cancel = Ui::CreateChild<Ui::CrossButton>(
		wrap,
		_st.searchRow.fieldCancel);
	registerToggleControlCallback(cancel, [=] {
		return !selectionMode() && searchMode();
	});

	auto cancelSearch = [=] {
		if (!field->getLastText().isEmpty()) {
			field->setText(QString());
		} else {
			_searchModeEnabled = false;
			updateControlsVisibility(anim::type::normal);
		}
	};

	cancel->addClickHandler(cancelSearch);
	field->connect(field, &Ui::InputField::cancelled, cancelSearch);

	wrap->widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		auto availableWidth = newWidth
			- _st.searchRow.fieldCancelSkip;
		fieldWrap->resizeToWidth(availableWidth);
		fieldWrap->moveToLeft(
			_st.searchRow.padding.left(),
			_st.searchRow.padding.top());
		cancel->moveToRight(0, 0);
	}, wrap->lifetime());

	widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		auto left = _back
			? _st.back.width
			: _st.titlePosition.x();
		wrap->setGeometryToLeft(
			left,
			0,
			newWidth - left,
			wrap->height(),
			newWidth);
	}, wrap->lifetime());

	field->alive(
	) | rpl::start_with_done([=] {
		field->setParent(nullptr);
		removeButton(search);
		clearSearchField();
	}, _searchView->lifetime());

	_searchModeEnabled = !field->getLastText().isEmpty() || startsFocused;
	updateControlsVisibility(anim::type::instant);

	std::move(
		shown
	) | rpl::start_with_next([=](bool visible) {
		auto alreadyInSearch = !field->getLastText().isEmpty();
		_searchModeAvailable = visible || alreadyInSearch;
		updateControlsVisibility(anim::type::instant);
	}, wrap->lifetime());
}

void TopBar::showSearch() {
	_searchModeEnabled = true;
	updateControlsVisibility(anim::type::normal);
}

void TopBar::removeButton(not_null<Ui::RpWidget*> button) {
	_buttons.erase(
		std::remove(_buttons.begin(), _buttons.end(), button),
		_buttons.end());
}

int TopBar::resizeGetHeight(int newWidth) {
	updateControlsGeometry(newWidth);
	return _st.height;
}

void TopBar::updateControlsGeometry(int newWidth) {
	updateDefaultControlsGeometry(newWidth);
	updateSelectionControlsGeometry(newWidth);
	updateStoriesGeometry(newWidth);
}

void TopBar::updateDefaultControlsGeometry(int newWidth) {
	auto right = 0;
	for (auto &button : _buttons) {
		if (!button) {
			continue;
		}
		button->moveToRight(right, 0, newWidth);
		right += button->width();
	}
	if (_back) {
		_back->setGeometryToLeft(
			0,
			0,
			newWidth - right,
			_back->height(),
			newWidth);
	}
	if (_title) {
		_title->moveToLeft(
			_back ? _st.back.width : _st.titlePosition.x(),
			_st.titlePosition.y(),
			newWidth);
	}
}

void TopBar::updateSelectionControlsGeometry(int newWidth) {
	if (!_selectionText) {
		return;
	}

	auto right = _st.mediaActionsSkip;
	if (_canDelete) {
		_delete->moveToRight(right, 0, newWidth);
		right += _delete->width();
	}
	if (_canForward) {
		_forward->moveToRight(right, 0, newWidth);
		right += _forward->width();
	}

	auto left = 0;
	_cancelSelection->moveToLeft(left, 0);
	left += _cancelSelection->width();

	const auto top = 0;
	const auto availableWidth = newWidth - left - right;
	_selectionText->resizeToNaturalWidth(availableWidth);
	_selectionText->moveToLeft(
		left,
		top,
		newWidth);
}

void TopBar::updateStoriesGeometry(int newWidth) {
	if (!_stories) {
		return;
	}

	auto right = 0;
	for (auto &button : _buttons) {
		if (!button) {
			continue;
		}
		button->moveToRight(right, 0, newWidth);
		right += button->width();
	}
	const auto left = (_back ? _st.back.width : _st.titlePosition.x())
		- st::dialogsStories.left - st::dialogsStories.photoLeft;
	const auto top = st::dialogsStories.height
		- st::dialogsStoriesFull.height
		+ (_st.height - st::dialogsStories.height) / 2;
	_stories->resizeToWidth(newWidth - left - right);
	_stories->moveToLeft(left, top, newWidth);
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto highlight = _a_highlight.value(_highlight ? 1. : 0.);
	if (_highlight && !_a_highlight.animating()) {
		_highlight = false;
		startHighlightAnimation();
	}
	if (!_roundRect) {
		const auto brush = anim::brush(_st.bg, _st.highlightBg, highlight);
		p.fillRect(e->rect(), brush);
	} else if (highlight > 0.) {
		p.setPen(Qt::NoPen);
		p.setBrush(anim::brush(_st.bg, _st.highlightBg, highlight));
		p.drawRoundedRect(
			rect() + style::margins(0, 0, 0, _st.radius * 2),
			_st.radius,
			_st.radius);
	} else {
		_roundRect->paintSomeRounded(
			p,
			rect(),
			RectPart::TopLeft | RectPart::TopRight);
	}
}

void TopBar::highlight() {
	_highlight = true;
	startHighlightAnimation();
}

void TopBar::startHighlightAnimation() {
	_a_highlight.start(
		[this] { update(); },
		_highlight ? 0. : 1.,
		_highlight ? 1. : 0.,
		_st.highlightDuration);
}

void TopBar::updateControlsVisibility(anim::type animated) {
	for (auto i = _updateControlCallbacks.begin(); i != _updateControlCallbacks.end();) {
		auto &&[widget, callback] = *i;
		if (!callback(animated)) {
			i = _updateControlCallbacks.erase(i);
		} else {
			++i;
		}
	}
}

void TopBar::setStories(rpl::producer<Dialogs::Stories::Content> content) {
	_storiesLifetime.destroy();
	if (content) {
		using namespace Dialogs::Stories;

		auto last = std::move(
			content
		) | rpl::start_spawning(_storiesLifetime);
		delete _stories;

		const auto stories = Ui::CreateChild<Ui::FadeWrap<List>>(
			this,
			object_ptr<List>(
				this,
				st::dialogsStoriesListInfo,
				rpl::duplicate(
					last
				) | rpl::filter([](const Content &content) {
					return !content.elements.empty();
				}),
				[] { return st::dialogsStories.height; }),
				st::infoTopBarScale);
		registerToggleControlCallback(
			stories,
			[this] { return _storiesCount > 0; });
		stories->toggle(false, anim::type::instant);
		stories->setDuration(st::infoTopBarDuration);
		_stories = stories;
		_stories->entity()->clicks(
		) | rpl::start_to_stream(_storyClicks, _stories->lifetime());
		if (_back) {
			_back->raise();
		}

		rpl::duplicate(
			last
		) | rpl::start_with_next([=](const Content &content) {
			const auto count = int(content.elements.size());
			if (_storiesCount != count) {
				const auto was = (_storiesCount > 0);
				_storiesCount = count;
				const auto now = (_storiesCount > 0);
				if (was != now) {
					updateControlsVisibility(anim::type::normal);
				}
				updateControlsGeometry(width());
			}
		}, _storiesLifetime);
	} else {
		_storiesCount = 0;
	}
	updateControlsVisibility(anim::type::instant);
}

void TopBar::setSelectedItems(SelectedItems &&items) {
	auto wasSelectionMode = selectionMode();
	_selectedItems = std::move(items);
	if (selectionMode()) {
		if (_selectionText) {
			updateSelectionState();
			if (!wasSelectionMode) {
				_selectionText->entity()->finishAnimating();
			}
		} else {
			createSelectionControls();
		}
	}
	updateControlsVisibility(anim::type::normal);
}

SelectedItems TopBar::takeSelectedItems() {
	_canDelete = false;
	_canForward = false;
	return std::move(_selectedItems);
}

rpl::producer<SelectionAction> TopBar::selectionActionRequests() const {
	return _selectionActionRequests.events();
}

void TopBar::updateSelectionState() {
	Expects(_selectionText && _delete && _forward);

	_canDelete = computeCanDelete();
	_canForward = computeCanForward();
	_selectionText->entity()->setValue(generateSelectedText());
	_delete->toggle(_canDelete, anim::type::instant);
	_forward->toggle(_canForward, anim::type::instant);

	updateSelectionControlsGeometry(width());
}

void TopBar::createSelectionControls() {
	auto wrap = [&](auto created) {
		registerToggleControlCallback(
			created,
			[this] { return selectionMode(); });
		created->toggle(false, anim::type::instant);
		return created;
	};
	_canDelete = computeCanDelete();
	_canForward = computeCanForward();
	_cancelSelection = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaCancel),
		st::infoTopBarScale));
	_cancelSelection->setDuration(st::infoTopBarDuration);
	_cancelSelection->entity()->clicks(
	) | rpl::map_to(
		SelectionAction::Clear
	) | rpl::start_to_stream(
		_selectionActionRequests,
		_cancelSelection->lifetime());
	_selectionText = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::LabelWithNumbers>>(
		this,
		object_ptr<Ui::LabelWithNumbers>(
			this,
			_st.title,
			_st.titlePosition.y(),
			generateSelectedText()),
		st::infoTopBarScale));
	_selectionText->setDuration(st::infoTopBarDuration);
	_selectionText->entity()->resize(0, _st.height);
	_forward = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaForward),
		st::infoTopBarScale));
	registerToggleControlCallback(
		_forward.data(),
		[this] { return selectionMode() && _canForward; });
	_forward->setDuration(st::infoTopBarDuration);
	_forward->entity()->clicks(
	) | rpl::map_to(
		SelectionAction::Forward
	) | rpl::start_to_stream(
		_selectionActionRequests,
		_cancelSelection->lifetime());
	_forward->entity()->setVisible(_canForward);
	_delete = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaDelete),
		st::infoTopBarScale));
	registerToggleControlCallback(
		_delete.data(),
		[this] { return selectionMode() && _canDelete; });
	_delete->setDuration(st::infoTopBarDuration);
	_delete->entity()->clicks(
	) | rpl::map_to(
		SelectionAction::Delete
	) | rpl::start_to_stream(
		_selectionActionRequests,
		_cancelSelection->lifetime());
	_delete->entity()->setVisible(_canDelete);

	updateControlsGeometry(width());
}

bool TopBar::computeCanDelete() const {
	return ranges::all_of(_selectedItems.list, &SelectedItem::canDelete);
}

bool TopBar::computeCanForward() const {
	return ranges::all_of(_selectedItems.list, &SelectedItem::canForward);
}

Ui::StringWithNumbers TopBar::generateSelectedText() const {
	using Type = Storage::SharedMediaType;
	const auto phrase = [&] {
		switch (_selectedItems.type) {
		case Type::Photo: return tr::lng_media_selected_photo;
		case Type::GIF: return tr::lng_media_selected_gif;
		case Type::Video: return tr::lng_media_selected_video;
		case Type::File: return tr::lng_media_selected_file;
		case Type::MusicFile: return tr::lng_media_selected_song;
		case Type::Link: return tr::lng_media_selected_link;
		case Type::RoundVoiceFile: return tr::lng_media_selected_audio;
			// #TODO stories
		case Type::PhotoVideo: return tr::lng_media_selected_photo;
		}
		Unexpected("Type in TopBar::generateSelectedText()");
	}();
	return phrase(
		tr::now,
		lt_count,
		_selectedItems.list.size(),
		Ui::StringWithNumbers::FromString);
}

bool TopBar::selectionMode() const {
	return !_selectedItems.list.empty();
}

bool TopBar::storiesTitle() const {
	return _storiesCount > 0;
}

bool TopBar::searchMode() const {
	return _searchModeAvailable && _searchModeEnabled;
}

void TopBar::performForward() {
	_selectionActionRequests.fire(SelectionAction::Forward);
}

void TopBar::performDelete() {
	_selectionActionRequests.fire(SelectionAction::Delete);
}

} // namespace Info
