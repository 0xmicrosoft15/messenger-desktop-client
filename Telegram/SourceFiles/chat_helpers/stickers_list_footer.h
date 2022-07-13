/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/clip/media_clip_reader.h"
#include "chat_helpers/tabbed_selector.h"
#include "ui/round_rect.h"

namespace Ui {
class InputField;
class CrossButton;
} // namespace Ui

namespace Data {
class StickersSet;
class StickersSetThumbnailView;
class DocumentMedia;
class CloudImageView;
} // namespace Data

namespace Lottie {
class SinglePlayer;
class FrameRenderer;
} // namespace Lottie

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {

enum class ValidateIconAnimations {
	Full,
	Scroll,
	None,
};

[[nodiscard]] uint64 EmojiSectionSetId(Ui::Emoji::Section section);
[[nodiscard]] uint64 RecentEmojiSectionSetId();
[[nodiscard]] uint64 AllEmojiSectionSetId();
[[nodiscard]] std::optional<Ui::Emoji::Section> SetIdEmojiSection(uint64 id);

struct StickerIcon {
	explicit StickerIcon(uint64 setId);
	StickerIcon(
		not_null<Data::StickersSet*> set,
		DocumentData *sticker,
		int pixw,
		int pixh);
	StickerIcon(StickerIcon&&);
	StickerIcon &operator=(StickerIcon&&);
	~StickerIcon();

	void ensureMediaCreated() const;

	uint64 setId = 0;
	Data::StickersSet *set = nullptr;
	mutable std::unique_ptr<Lottie::SinglePlayer> lottie;
	mutable Media::Clip::ReaderPointer webm;
	mutable QPixmap savedFrame;
	DocumentData *sticker = nullptr;
	ChannelData *megagroup = nullptr;
	mutable std::shared_ptr<Data::StickersSetThumbnailView> thumbnailMedia;
	mutable std::shared_ptr<Data::DocumentMedia> stickerMedia;
	mutable std::shared_ptr<Data::CloudImageView> megagroupUserpic;
	int pixw = 0;
	int pixh = 0;
	mutable rpl::lifetime lifetime;
};

class StickersListFooter final : public TabbedSelector::InnerFooter {
public:
	struct Descriptor {
		not_null<Window::SessionController*> controller;
		not_null<RpWidget*> parent;
		bool searchButtonVisible = false;
		bool settingsButtonVisible = false;
		bool barSelection = false;
	};
	explicit StickersListFooter(Descriptor &&descriptor);

	void preloadImages();
	void validateSelectedIcon(
		uint64 setId,
		ValidateIconAnimations animations);
	void refreshIcons(
		std::vector<StickerIcon> icons,
		Fn<std::shared_ptr<Lottie::FrameRenderer>()> renderer,
		ValidateIconAnimations animations);
	[[nodiscard]] bool hasOnlyFeaturedSets() const;

	void leaveToChildEvent(QEvent *e, QWidget *child) override;

	void stealFocus();
	void returnFocus();
	void setLoading(bool loading);

	void clearHeavyData();

	[[nodiscard]] rpl::producer<uint64> setChosen() const;
	[[nodiscard]] rpl::producer<> openSettingsRequests() const;

	struct SearchRequest {
		QString text;
		bool forced = false;
	};
	[[nodiscard]] rpl::producer<SearchRequest> searchRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	bool eventHook(QEvent *e) override;

	void processHideFinished() override;

private:
	enum class SpecialOver {
		None,
		Search,
		Settings,
	};
	struct IconId {
		int index = 0;
		int subindex = 0;

		friend inline bool operator==(IconId a, IconId b) {
			return (a.index == b.index) && (a.subindex == b.subindex);
		}
	};
	using OverState = std::variant<SpecialOver, IconId>;
	struct IconInfo {
		int index = 0;
		int left = 0;
		int width = 0;
		bool visible = false;
	};
	struct ScrollState {
		int selected = 0;
		int max = 0;
		anim::value x;
		anim::value selectionX;
		anim::value selectionWidth;
		crl::time animationStart = 0;
	};

	void enumerateVisibleIcons(Fn<void(const IconInfo &)> callback) const;
	void enumerateIcons(Fn<bool(const IconInfo &)> callback) const;
	void enumerateSubicons(Fn<bool(const IconInfo &)> callback) const;
	[[nodiscard]] IconInfo iconInfo(int index) const;
	[[nodiscard]] IconInfo subiconInfo(int index) const;

	[[nodiscard]] std::shared_ptr<Lottie::FrameRenderer> getLottieRenderer();
	bool iconsAnimationCallback(ScrollState &state, crl::time now);
	void setSelectedIcon(
		int newSelected,
		ValidateIconAnimations animations);
	void setSelectedSubicon(
		int newSelected,
		ValidateIconAnimations animations);
	void validateIconLottieAnimation(const StickerIcon &icon);
	void validateIconWebmAnimation(const StickerIcon &icon);
	void validateIconAnimation(const StickerIcon &icon);

	void refreshIconsGeometry(ValidateIconAnimations animations);
	void refreshSubiconsGeometry();
	void updateSelected();
	void updateSetIcon(uint64 setId);
	void updateSetIconAt(int left);
	void finishDragging();
	void paintStickerSettingsIcon(Painter &p) const;
	void paintSearchIcon(Painter &p) const;
	void paintSetIcon(
		Painter &p,
		const IconInfo &info,
		crl::time now,
		bool paused) const;
	void paintSelectionBg(Painter &p) const;
	void paintSelectionBar(Painter &p) const;
	void paintLeftRightFading(Painter &p) const;
	void validatePremiumIcon() const;

	void updateEmojiSectionWidth();
	void updateEmojiWidthCallback();

	void initSearch();
	void toggleSearch(bool visible);
	void resizeSearchControls();
	void scrollByWheelEvent(not_null<QWheelEvent*> e);

	void clipCallback(Media::Clip::Notification notification, uint64 setId);

	const not_null<Window::SessionController*> _controller;
	const bool _searchButtonVisible = false;
	const bool _settingsButtonVisible = false;

	static constexpr auto kVisibleIconsCount = 8;

	std::weak_ptr<Lottie::FrameRenderer> _lottieRenderer;
	std::vector<StickerIcon> _icons;
	Fn<std::shared_ptr<Lottie::FrameRenderer>()> _renderer;
	uint64 _activeByScrollId = 0;
	OverState _selected = SpecialOver::None;
	OverState _pressed = SpecialOver::None;

	QPoint _iconsMousePos, _iconsMouseDown;
	mutable QImage _premiumIcon;
	int _iconsLeft = 0;
	int _iconsRight = 0;
	int _iconsTop = 0;

	int _iconsStartX = 0;
	bool _iconsDragging = false;

	ScrollState _iconState;
	Ui::Animations::Basic _iconsAnimation;

	ScrollState _subiconState;
	Ui::Animations::Basic _subiconsAnimation;

	Ui::RoundRect _selectionBg;
	Ui::Animations::Simple _subiconsWidthAnimation;
	int _subiconsWidth = 0;
	bool _subiconsExpanded = false;
	bool _barSelection = false;

	bool _horizontal = false;

	bool _searchShown = false;
	object_ptr<Ui::InputField> _searchField = { nullptr };
	object_ptr<Ui::CrossButton> _searchCancel = { nullptr };
	QPointer<QWidget> _focusTakenFrom;

	rpl::event_stream<> _openSettingsRequests;
	rpl::event_stream<uint64> _setChosen;
	rpl::event_stream<SearchRequest> _searchRequests;

};

} // namespace ChatHelpers
