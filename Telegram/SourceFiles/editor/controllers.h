/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/stickers_panel_controller.h"
#include "editor/undo_controller.h"

namespace Editor {

struct Controllers final {
	Controllers(
		std::unique_ptr<StickersPanelController> stickersPanelController,
		std::unique_ptr<UndoController> undoController)
	: stickersPanelController(std::move(stickersPanelController))
	, undoController(std::move(undoController)) {
	}
	~Controllers() {
	};

	const std::unique_ptr<StickersPanelController> stickersPanelController;
	const std::unique_ptr<UndoController> undoController;
};

} // namespace Editor
