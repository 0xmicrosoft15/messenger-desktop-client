/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_media.h"
#include "data/data_document.h"
#include "data/data_photo.h"

class HistoryGroupedMedia : public HistoryMedia {
public:
	HistoryGroupedMedia(
		not_null<Element*> parent,
		const std::vector<not_null<Element*>> &others);

	HistoryMediaType type() const override {
		return MediaTypeGrouped;
	}

	void refreshParentId(not_null<Element*> realParent) override;

	void draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		TimeMs ms) const override;
	HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const override;

	bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	bool dragItemByHandler(const ClickHandlerPtr &p) const override;

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override {
		return _caption.length();
	}
	bool hasTextForCopy() const override {
		return !_caption.isEmpty();
	}

	PhotoData *getPhoto() const override;
	DocumentData *getDocument() const override;

	TextWithEntities selectedText(TextSelection selection) const override;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	void attachToParent() override;
	void detachFromParent() override;
	std::unique_ptr<HistoryMedia> takeLastFromGroup() override;
	bool applyGroup(
		const std::vector<not_null<Element*>> &others) override;

	bool hasReplyPreview() const override;
	ImagePtr replyPreview() override;
	TextWithEntities getCaption() const override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	bool overrideEditedDate() const override {
		return true;
	}
	HistoryMessageEdited *displayedEditBadge() const override;

	bool canBeGrouped() const override {
		return true;
	}

	bool skipBubbleTail() const override {
		return isBubbleBottom() && _caption.isEmpty();
	}
	void updateNeedBubbleState() override;
	bool needsBubble() const override;
	bool customInfoLayout() const override {
		return _caption.isEmpty();
	}
	bool allowsFastShare() const override {
		return true;
	}

private:
	struct Part {
		Part(not_null<Element*> view);

		not_null<Element*> view;
		std::unique_ptr<HistoryMedia> content;

		RectParts sides = RectPart::None;
		QRect initialGeometry;
		QRect geometry;
		mutable uint64 cacheKey = 0;
		mutable QPixmap cache;

	};

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	bool needInfoDisplay() const;
	bool computeNeedBubble() const;
	not_null<HistoryMedia*> main() const;
	bool validateGroupParts(
		const std::vector<not_null<Element*>> &others) const;
	HistoryTextState getPartState(
		QPoint point,
		HistoryStateRequest request) const;

	Text _caption;
	std::vector<Part> _parts;
	bool _needBubble = false;

};
