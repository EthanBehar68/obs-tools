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
#include "net/device-login.hpp"

#include <QDialog>

#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace unified_chat {

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(const ChatConfig &config, QWidget *parent = nullptr);
	~SettingsDialog() override;

	ChatConfig Result() const;

private:
	void StartLogin(Platform platform);
	void FinishLogin(Platform platform, const oauth::Token &token, const QString &login, const QString &error);
	void SignOut(Platform platform);
	void UpdateAccountLabels();

	ChatConfig config_;
	std::unique_ptr<DeviceLogin> login_;
	Platform loginPlatform_ = Platform::Twitch;

	QLineEdit *twitchChannel_;
	QLineEdit *twitchClientId_;
	QLabel *twitchAccount_;
	QPushButton *twitchSignIn_;
	QPushButton *twitchSignOut_;

	QLineEdit *youtubeClientId_;
	QLineEdit *youtubeClientSecret_;
	QLineEdit *youtubeVideo_;
	QSpinBox *youtubePoll_;
	QLabel *youtubeAccount_;
	QPushButton *youtubeSignIn_;
	QPushButton *youtubeSignOut_;

	QSpinBox *maxMessages_;
	QLineEdit *mergeBots_;
};

} // namespace unified_chat
