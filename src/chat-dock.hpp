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
#include "core/mentions.hpp"
#include "core/name-color.hpp"
#include "net/twitch-connection.hpp"
#include "net/youtube-connection.hpp"

#include <QColor>
#include <QString>
#include <QWidget>

#include <memory>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QTextBlock;
class QTextBrowser;
class QTimer;
class QToolButton;
class QUrl;

namespace unified_chat {

class TargetSwitch;
struct ModerationTag;

class ChatDock : public QWidget {
	Q_OBJECT

public:
	explicit ChatDock(QWidget *parent = nullptr);
	~ChatDock() override;

	void Start();
	void Shutdown();
	// OBS's main output started or stopped streaming (the Start Streaming button).
	void OnStreamingChanged(bool streaming);

protected:
	void changeEvent(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	// What a chat line is, for moderation: which platforms it's on, which message, whose, and where its text is.
	struct LineInfo {
		bool twitch = false;
		bool youtube = false;
		std::string messageId; // of the message's own platform
		std::string login;     // lower case, Twitch
		std::string authorId;
		std::string author; // display name, for notices
		int textLength = 0; // UTF-16 length of the message text, which ends the line
		int severity = 0;   // of the moderation tag shown, 0 = none
		int tagLength = 0;  // UTF-16 length of that tag, which follows the text
	};

	struct LineData; // QTextBlockUserData holding a LineInfo; owned by its text block

	struct HtmlLine {
		QString html;
		int lineId = -1;              // tags the line so ReplaceLine can find it later
		bool highlight = false;       // mentions you
		std::optional<LineInfo> info; // chat lines only, not notices
	};

	// A Twitch reply being written: the message it answers.
	struct PendingReply {
		std::string messageId;
		std::string mention;
	};

	void LoadConfig();
	void SaveConfig();
	void Connect();
	void ConnectYouTube();
	void Disconnect();
	void OpenSettings();
	void SendCurrent();

	void AppendMessages(const std::vector<ChatMessage> &messages);
	void AppendLines(const std::vector<DisplayLine> &lines);
	void ScheduleEchoTimer();
	void AppendNotice(const QString &text);
	// Inserts all lines in one edit block, so the view lays out and scrolls once per batch.
	void AppendHtml(const std::vector<HtmlLine> &lines);
	bool ReplaceLine(int lineId, const QString &html, const DisplayLine &line);
	static LineInfo MakeLineInfo(const DisplayLine &line);
	// Strikes the matching lines through, tags them and, for timeouts, bans and clears, adds a notice.
	void ApplyModeration(const ModerationEvent &event);
	static void StrikeLine(const QTextBlock &block, LineInfo &info, const ModerationTag &tag);
	void SetLinkState(Platform platform, LinkState state);
	void UpdatePlaceholder();

	// Mentions and replies
	void OnLinkClicked(const QUrl &url);
	void SwitchForMention(Platform platform); // temporary, undone by RestoreTarget
	void RestoreTarget();
	void SetReply(std::optional<PendingReply> reply);
	bool CompleteMention(); // "@" + Tab; returns false when there's nothing to complete
	// The "@word" the cursor is in: its range in the input and the text after '@'. False when there is none.
	bool CurrentMentionWord(int &start, int &end, QString &prefix) const;
	std::optional<Platform> MentionCandidatesPlatform() const; // nullopt = both platforms
	void UpdateSuggestions(); // the list shown while typing "@" plus at least one letter
	void ApplySuggestion(int row);
	void HideSuggestions();
	void LearnOwnName(const ChatMessage &message);
	void UpdateMentionNames();
	void RegisterIcons();
	ConnectionCallbacks MakeCallbacks(Platform platform);

	ChatConfig config_;
	std::unique_ptr<TwitchConnection> twitch_;
	std::unique_ptr<YouTubeConnection> youtube_;
	LinkState twitchState_ = LinkState::Disconnected;
	LinkState youtubeState_ = LinkState::Disconnected;
	bool started_ = false;
	bool obsStreaming_ = false;
	int64_t historyCutoff_ = 0; // YouTube messages posted before this (unix seconds) aren't shown
	bool empty_ = true;
	int iconSize_ = 16;
	EchoMerger merger_;
	QTimer *echoTimer_;
	NameColorResolver nameColors_;
	BotMerger botMerger_;
	int nextLineId_ = 1;
	bool backgroundStale_ = true; // the theme's stylesheet sets the view's real background at polish time
	QColor highlightColor_;

	MentionMatcher mentions_;
	std::vector<std::string> ownYouTubeNames_; // learned from your own YouTube messages
	RecentChatters chatters_;
	std::optional<PendingReply> reply_;
	MentionTarget mentionTarget_;
	// Tab completion in progress: candidates, which one is shown, and the text range it occupies.
	std::vector<RecentChatters::Entry> completions_;
	size_t completionIndex_ = 0;
	int completionStart_ = 0;
	int completionEnd_ = 0;

	QToolButton *twitchStatus_;
	QToolButton *youtubeStatus_;
	QToolButton *clearButton_;
	QToolButton *settingsButton_;
	QTextBrowser *view_;
	QWidget *replyBar_;
	QLabel *replyLabel_;
	QListWidget *suggestions_; // floats above the input; never takes focus from it
	QLineEdit *input_;
	TargetSwitch *target_;
};

} // namespace unified_chat
