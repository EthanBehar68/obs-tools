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
#include "core/bot-merger.hpp"
#include "core/echo-merger.hpp"
#include "core/name-color.hpp"
#include "net/twitch-connection.hpp"
#include "net/youtube-connection.hpp"

#include <QStringList>
#include <QWidget>

#include <memory>

class QLineEdit;
class QTextBrowser;
class QTimer;
class QToolButton;

namespace unified_chat {

class TargetSwitch;

class ChatDock : public QWidget {
	Q_OBJECT

public:
	explicit ChatDock(QWidget *parent = nullptr);
	~ChatDock() override;

	void Start();
	void Shutdown();

protected:
	void changeEvent(QEvent *event) override;

private:
	void LoadConfig();
	void SaveConfig();
	void Connect();
	void Disconnect();
	void OpenSettings();
	void SendCurrent();

	void AppendMessages(const std::vector<ChatMessage> &messages);
	void AppendLines(const std::vector<DisplayLine> &lines);
	void ScheduleEchoTimer();
	void AppendNotice(const QString &text);
	// Inserts all lines in one edit block, so the view lays out and scrolls once per batch.
	// lineIds[i], when given and not -1, tags line i so ReplaceLine can find it later.
	void AppendHtml(const QStringList &lines, const std::vector<int> &lineIds = {});
	bool ReplaceLine(int lineId, const QString &html);
	void SetLinkState(Platform platform, LinkState state);
	void UpdatePlaceholder();
	void RegisterIcons();
	ConnectionCallbacks MakeCallbacks(Platform platform);

	ChatConfig config_;
	std::unique_ptr<TwitchConnection> twitch_;
	std::unique_ptr<YouTubeConnection> youtube_;
	LinkState twitchState_ = LinkState::Disconnected;
	LinkState youtubeState_ = LinkState::Disconnected;
	bool started_ = false;
	bool empty_ = true;
	int iconSize_ = 16;
	EchoMerger merger_;
	QTimer *echoTimer_;
	NameColorResolver nameColors_;
	BotMerger botMerger_;
	int nextLineId_ = 1;
	bool backgroundStale_ = true; // the theme's stylesheet sets the view's real background at polish time

	QToolButton *twitchStatus_;
	QToolButton *youtubeStatus_;
	QToolButton *clearButton_;
	QToolButton *settingsButton_;
	QTextBrowser *view_;
	QLineEdit *input_;
	TargetSwitch *target_;
};

} // namespace unified_chat
