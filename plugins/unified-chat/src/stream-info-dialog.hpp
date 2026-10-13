/*
obs-unified-chat
Copyright (C) 2026 ebehar

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include "core/obs-tools.hpp"

#include "core/chat-config.hpp"
#include "net/stream-info-client.hpp"

#include <QDialog>
#include <QHash>
#include <QPixmap>

#include <functional>
#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QSpinBox;
class QToolButton;
class QPushButton;
class QTimer;

namespace unified_chat {

// Tools > OBS Tools > Stream Info... and the dock's ✎ button: the title for Twitch and YouTube, the Twitch category
// (searched, with box art) and tags, and the YouTube category. Not modal; its requests run on a thread of its own
// that ends when the window closes.
class StreamInfoDialog : public QDialog {
public:
	// Runs when the platforms to change, the recent categories or the scene links change, for config.json. Only
	// those fields of the config are meant.
	using SaveSettings = std::function<void(const ChatConfig &settings)>;

	StreamInfoDialog(const ChatConfig &config, SaveSettings save, QWidget *parent);
	~StreamInfoDialog() override;

	// Reads everything again, dropping unsaved edits (after signing in again, or the Reload button).
	void Reload();

private:
	void OnTwitch(StreamInfoClient::TwitchState state);
	void OnYouTube(StreamInfoClient::YouTubeState state);
	void OnCategories(const std::string &query, const std::vector<TwitchCategory> &categories);
	void OnImage(const std::string &url, const std::string &bytes);
	void OnApplied(Platform platform, const std::string &error);

	void ShowCategories(const std::vector<TwitchCategory> &categories, bool recent);
	void SelectCategory(const TwitchCategory &category, const std::vector<std::string> *tags);
	QPixmap BoxArt(const TwitchCategory &category);
	void UpdateTitleCount();
	void UpdateControls();
	void Apply();
	void Save();
	// The scenes linked to a category, with today's scene names.
	void ShowSceneLinks();
	void FillSceneMenu();

	SaveSettings save_;
	std::vector<RecentCategory> recent_;
	std::vector<SceneCategory> sceneLinks_;
	std::unique_ptr<StreamInfoClient> client_;
	std::optional<StreamInfoClient::TwitchState> twitch_;
	std::optional<StreamInfoClient::YouTubeState> youtube_;
	TwitchCategory selected_;
	bool titleEdited_ = false;
	int applying_ = 0;               // platforms whose answer is still to come
	QHash<QString, QPixmap> boxArt_; // by URL
	QHash<QString, bool> requested_;

	QLabel *twitchStatus_;
	QLabel *youtubeStatus_;
	QLineEdit *title_;
	QLabel *titleCount_;
	QLineEdit *search_;
	QListWidget *categories_;
	QLabel *categoryIcon_;
	QLabel *category_;
	QLineEdit *tags_;
	QLabel *tagsWarning_;
	QComboBox *youtubeCategory_;
	QToolButton *useForScene_;
	QMenu *sceneMenu_;
	QCheckBox *byScene_;
	QSpinBox *sceneDelay_;
	QListWidget *sceneList_;
	QPushButton *unlink_;
	QCheckBox *changeTwitch_;
	QCheckBox *changeYouTube_;
	QPushButton *signIn_;
	QPushButton *apply_;
	QLabel *result_;
	QTimer *searchTimer_;
};

} // namespace unified_chat
