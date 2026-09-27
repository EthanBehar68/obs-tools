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

#include "core/chat-config.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace unified_chat {

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(const ChatConfig &config, QWidget *parent = nullptr);

	ChatConfig Result() const;

private:
	QLayout *AccountRow(QLabel *label, QWidget *parent);
	void OpenAccounts();
	void UpdateAccountLabels();

	ChatConfig config_;

	QLineEdit *twitchChannel_;
	QLabel *twitchAccount_;

	QLineEdit *youtubeVideo_;
	QComboBox *youtubeConnect_;
	QComboBox *youtubeMethod_;
	QSpinBox *youtubePoll_;
	QLabel *youtubeAccount_;

	QSpinBox *maxMessages_;
	QLineEdit *mergeBots_;
	QCheckBox *showAlerts_;
};

} // namespace unified_chat
