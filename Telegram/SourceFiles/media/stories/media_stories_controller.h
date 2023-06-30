/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_stories.h"
#include "ui/effects/animations.h"

namespace base {
class PowerSaveBlocker;
} // namespace base

namespace ChatHelpers {
class Show;
struct FileChosen;
} // namespace ChatHelpers

namespace Data {
struct FileOrigin;
struct ReactionId;
} // namespace Data

namespace HistoryView::Reactions {
class CachedIconFactory;
} // namespace HistoryView::Reactions

namespace Ui {
class RpWidget;
struct MessageSendingAnimationFrom;
class EmojiFlyAnimation;
} // namespace Ui

namespace Ui::Toast {
struct Config;
} // namespace Ui::Toast

namespace Main {
class Session;
} // namespace Main

namespace Media::Player {
struct TrackState;
} // namespace Media::Player

namespace Media::Stories {

class Header;
class Slider;
class ReplyArea;
class Reactions;
class RecentViews;
class Sibling;
class Delegate;
struct SiblingView;
enum class SiblingType;
struct ContentLayout;
class CaptionFullView;

enum class HeaderLayout {
	Normal,
	Outside,
};

struct SiblingLayout {
	QRect geometry;
	QRect userpic;
	QRect nameBoundingRect;
	int nameFontSize = 0;

	friend inline bool operator==(SiblingLayout, SiblingLayout) = default;
};

struct Layout {
	QRect content;
	QRect header;
	QRect slider;
	QRect reactions;
	int controlsWidth = 0;
	QPoint controlsBottomPosition;
	QRect views;
	QRect autocompleteRect;
	HeaderLayout headerLayout = HeaderLayout::Normal;
	SiblingLayout siblingLeft;
	SiblingLayout siblingRight;

	friend inline bool operator==(Layout, Layout) = default;
};

struct ViewsSlice {
	std::vector<Data::StoryView> list;
	int left = 0;
};

class Controller final : public base::has_weak_ptr {
public:
	explicit Controller(not_null<Delegate*> delegate);
	~Controller();

	[[nodiscard]] Data::Story *story() const;
	[[nodiscard]] not_null<Ui::RpWidget*> wrap() const;
	[[nodiscard]] Layout layout() const;
	[[nodiscard]] rpl::producer<Layout> layoutValue() const;
	[[nodiscard]] ContentLayout contentLayout() const;
	[[nodiscard]] bool closeByClickAt(QPoint position) const;
	[[nodiscard]] Data::FileOrigin fileOrigin() const;
	[[nodiscard]] TextWithEntities captionText() const;
	void showFullCaption();

	[[nodiscard]] std::shared_ptr<ChatHelpers::Show> uiShow() const;
	[[nodiscard]] auto stickerOrEmojiChosen() const
	-> rpl::producer<ChatHelpers::FileChosen>;
	[[nodiscard]] auto cachedReactionIconFactory() const
		-> HistoryView::Reactions::CachedIconFactory &;

	void show(not_null<Data::Story*> story, Data::StoriesContext context);
	void ready();

	void updateVideoPlayback(const Player::TrackState &state);

	[[nodiscard]] bool subjumpAvailable(int delta) const;
	[[nodiscard]] bool subjumpFor(int delta);
	[[nodiscard]] bool jumpFor(int delta);
	[[nodiscard]] bool paused() const;
	void togglePaused(bool paused);
	void contentPressed(bool pressed);
	void setMenuShown(bool shown);

	void repaintSibling(not_null<Sibling*> sibling);
	[[nodiscard]] SiblingView sibling(SiblingType type) const;

	[[nodiscard]] ViewsSlice views(PeerId offset);
	[[nodiscard]] rpl::producer<> moreViewsLoaded() const;

	void unfocusReply();
	void shareRequested();
	void deleteRequested();
	void reportRequested();
	void togglePinnedRequested(bool pinned);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct StoriesList {
		not_null<UserData*> user;
		Data::StoriesIds ids;
		int total = 0;

		friend inline bool operator==(
			const StoriesList &,
			const StoriesList &) = default;
	};
	class PhotoPlayback;
	class Unsupported;

	void initLayout();
	void updatePhotoPlayback(const Player::TrackState &state);
	void updatePlayback(const Player::TrackState &state);
	void updatePowerSaveBlocker(const Player::TrackState &state);
	void maybeMarkAsRead(const Player::TrackState &state);
	void markAsRead();

	void updateContentFaded();
	void updatePlayingAllowed();
	void setPlayingAllowed(bool allowed);

	void hideSiblings();
	void showSiblings(not_null<Main::Session*> session);
	void showSibling(
		std::unique_ptr<Sibling> &sibling,
		not_null<Main::Session*> session,
		PeerId peerId);

	void subjumpTo(int index);
	void checkWaitingFor();
	void moveFromShown();

	void refreshViewsFromData();
	bool sliceViewsTo(PeerId offset);
	[[nodiscard]] auto viewsGotMoreCallback()
		-> Fn<void(std::vector<Data::StoryView>)>;

	[[nodiscard]] bool shown() const;
	[[nodiscard]] UserData *shownUser() const;
	[[nodiscard]] int shownCount() const;
	[[nodiscard]] StoryId shownId(int index) const;
	void rebuildFromContext(not_null<UserData*> user, FullStoryId storyId);
	void checkMoveByDelta();
	void loadMoreToList();
	void preloadNext();
	void rebuildCachedSourcesList(
		const std::vector<Data::StoriesSourceInfo> &lists,
		int index);

	void startReactionAnimation(
		Data::ReactionId id,
		Ui::MessageSendingAnimationFrom from);

	const not_null<Delegate*> _delegate;

	rpl::variable<std::optional<Layout>> _layout;

	const not_null<Ui::RpWidget*> _wrap;
	const std::unique_ptr<Header> _header;
	const std::unique_ptr<Slider> _slider;
	const std::unique_ptr<ReplyArea> _replyArea;
	const std::unique_ptr<Reactions> _reactions;
	const std::unique_ptr<RecentViews> _recentViews;
	std::unique_ptr<Unsupported> _unsupported;
	std::unique_ptr<PhotoPlayback> _photoPlayback;
	std::unique_ptr<CaptionFullView> _captionFullView;

	Ui::Animations::Simple _contentFadeAnimation;
	bool _contentFaded = false;

	bool _windowActive = false;
	bool _replyFocused = false;
	bool _replyActive = false;
	bool _hasSendText = false;
	bool _layerShown = false;
	bool _menuShown = false;
	bool _paused = false;

	FullStoryId _shown;
	TextWithEntities _captionText;
	Data::StoriesContext _context;
	std::optional<Data::StoriesSource> _source;
	std::optional<StoriesList> _list;
	FullStoryId _waitingForId;
	int _waitingForDelta = 0;
	int _index = 0;
	bool _started = false;
	bool _viewed = false;

	std::vector<PeerId> _cachedSourcesList;
	int _cachedSourceIndex = -1;

	ViewsSlice _viewsSlice;
	rpl::event_stream<> _moreViewsLoaded;
	base::has_weak_ptr _viewsLoadGuard;

	std::unique_ptr<Sibling> _siblingLeft;
	std::unique_ptr<Sibling> _siblingRight;

	std::unique_ptr<base::PowerSaveBlocker> _powerSaveBlocker;
	std::unique_ptr<Ui::EmojiFlyAnimation> _reactionAnimation;

	Main::Session *_session = nullptr;
	rpl::lifetime _sessionLifetime;

	rpl::lifetime _contextLifetime;

	rpl::lifetime _lifetime;

};

[[nodiscard]] Ui::Toast::Config PrepareTogglePinnedToast(
	int count,
	bool pinned);

} // namespace Media::Stories
