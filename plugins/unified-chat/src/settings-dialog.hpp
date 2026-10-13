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

#include <QDialog>

#include <array>
#include <string>

class QCheckBox;
class QComboBox;
class QLayout;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTextBrowser;
class QToolButton;

namespace unified_chat {

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	// chatBackground: the dock's background ("#rrggbb"), so the Highlights preview looks like the dock.
	SettingsDialog(const ChatConfig &config, const std::string &chatBackground, QWidget *parent = nullptr);

	ChatConfig Result() const;

private:
	QLayout *AccountRow(QLabel *label, QWidget *parent);
	void OpenAccounts();
	void UpdateAccountLabels();
	QWidget *MakeHighlightsTab(QWidget *parent);
	void ShowHighlight(size_t kind); // a row's controls from highlights_
	void UpdatePreview();

	ChatConfig config_;

	QLineEdit *twitchChannel_;
	QComboBox *twitchMethod_;
	QLabel *twitchAccount_;

	QLineEdit *youtubeVideo_;
	QComboBox *youtubeConnect_;
	QComboBox *youtubeMethod_;
	QSpinBox *youtubePoll_;
	QLabel *youtubeAccount_;

	QSpinBox *maxMessages_;
	QLineEdit *mergeBots_;
	QCheckBox *showAlerts_;
	QSpinBox *viewerCheck_;

	// Highlights tab: a working copy, written back by Result().
	struct HighlightRow {
		QCheckBox *show = nullptr;
		QToolButton *color = nullptr;
		QLineEdit *icon = nullptr;
	};
	Highlights highlights_;
	std::array<HighlightRow, kHighlightKinds> highlightRows_;
	QTextBrowser *preview_ = nullptr;
	std::string chatBackground_;
};

} // namespace unified_chat
