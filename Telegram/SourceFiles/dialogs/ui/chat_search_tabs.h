/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class SettingsSlider;
class PlainShadow;
} // namespace Ui

namespace Dialogs {

enum class ChatSearchTab : uchar {
	MyMessages,
	ThisTopic,
	ThisPeer,
	PublicPosts,
};

enum class ChatSearchPeerTabType : uchar {
	Chat,
	Channel,
	Group,
};

// Available for MyMessages and PublicPosts.
[[nodiscard]] TextWithEntities DefaultShortLabel(ChatSearchTab tab);

class ChatSearchTabs final : public Ui::RpWidget {
public:
	ChatSearchTabs(QWidget *parent, ChatSearchTab active);
	~ChatSearchTabs();

	void setPeerTabType(ChatSearchPeerTabType type);

	// A [custom] emoji to use when there is not enough space for text.
	// Only tabs with available short labels are shown.
	struct ShortLabel {
		ChatSearchTab tab = {};
		TextWithEntities label;
	};
	void setTabShortLabels(
		std::vector<ShortLabel> labels,
		ChatSearchTab active);

	[[nodiscard]] rpl::producer<ChatSearchTab> tabChanges() const;

private:
	struct Tab {
		ChatSearchTab value = {};
		QString label;
		TextWithEntities shortLabel;
	};

	void refreshTabs(ChatSearchTab active);
	int resizeGetHeight(int newWidth) override;

	const std::unique_ptr<Ui::SettingsSlider> _tabs;
	const std::unique_ptr<Ui::PlainShadow> _shadow;

	std::vector<Tab> _list;
	rpl::variable<ChatSearchTab> _active;
	ChatSearchPeerTabType _type = {};

};

struct FixedHashtagSearchQuery {
	QString text;
	int cursorPosition = 0;
};
[[nodiscard]] FixedHashtagSearchQuery FixHashtagSearchQuery(
	const QString &query,
	int cursorPosition);

[[nodiscard]] bool IsHashtagSearchQuery(const QString &query);

} // namespace Dialogs
