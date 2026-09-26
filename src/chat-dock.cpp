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

#include "chat-dock.hpp"
#include "platform-icons.hpp"
#include "settings-dialog.hpp"
#include "target-switch.hpp"
#include "core/chat-format.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QDateTime>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <ctime>
#include <iterator>
#include <limits>

namespace unified_chat {

// Allowance for clock differences between this PC and YouTube when deciding what counts as chat history.
static constexpr int64_t kHistoryGraceSeconds = 30;

static QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static const char *StateKey(LinkState state)
{
	switch (state) {
	case LinkState::Connected:
		return "Status.Connected";
	case LinkState::ReadOnly:
		return "Status.ReadOnly";
	case LinkState::Connecting:
		return "Status.Connecting";
	case LinkState::Standby:
		return "Status.Standby";
	default:
		return "Status.Offline";
	}
}

static QToolButton *MakeStatusButton(Platform platform, QWidget *parent)
{
	auto button = new QToolButton(parent);
	button->setIcon(PlatformIcon(platform));
	button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	button->setAutoRaise(true);
	return button;
}

ChatDock::ChatDock(QWidget *parent) : QWidget(parent)
{
	setWindowTitle(Text("Title"));

	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto header = new QHBoxLayout();
	twitchStatus_ = MakeStatusButton(Platform::Twitch, this);
	youtubeStatus_ = MakeStatusButton(Platform::YouTube, this);
	clearButton_ = new QToolButton(this);
	clearButton_->setText(Text("Btn.Clear"));
	clearButton_->setAutoRaise(true);
	settingsButton_ = new QToolButton(this);
	settingsButton_->setText(Text("Btn.Settings"));
	settingsButton_->setAutoRaise(true);
	header->addWidget(twitchStatus_);
	header->addWidget(youtubeStatus_);
	header->addStretch();
	header->addWidget(clearButton_);
	header->addWidget(settingsButton_);
	layout->addLayout(header);

	view_ = new QTextBrowser(this);
	view_->setOpenLinks(false);
	view_->setUndoRedoEnabled(false);
	view_->document()->setDocumentMargin(4);
	layout->addWidget(view_, 1);

	auto footer = new QHBoxLayout();
	input_ = new QLineEdit(this);
	input_->setMaxLength(500);
	target_ = new TargetSwitch(this);
	footer->addWidget(input_, 1);
	footer->addWidget(target_);
	layout->addLayout(footer);

	iconSize_ = qMax(14, fontMetrics().height());

	echoTimer_ = new QTimer(this);
	echoTimer_->setSingleShot(true); // armed for the next merge deadline only, see ScheduleEchoTimer
	connect(echoTimer_, &QTimer::timeout, this,
		[this]() { AppendLines(merger_.Expire(QDateTime::currentMSecsSinceEpoch())); });
	RegisterIcons();

	connect(input_, &QLineEdit::returnPressed, this, &ChatDock::SendCurrent);
	connect(target_, &TargetSwitch::TargetChanged, this, [this]() {
		config_.sendTarget = target_->Target();
		UpdatePlaceholder();
		SaveConfig();
	});
	connect(settingsButton_, &QToolButton::clicked, this, &ChatDock::OpenSettings);
	connect(twitchStatus_, &QToolButton::clicked, this, &ChatDock::OpenSettings);
	connect(youtubeStatus_, &QToolButton::clicked, this, &ChatDock::OpenSettings);
	connect(clearButton_, &QToolButton::clicked, this, [this]() {
		view_->clear();
		empty_ = true;
		RegisterIcons();
	});

	SetLinkState(Platform::Twitch, LinkState::Disconnected);
	SetLinkState(Platform::YouTube, LinkState::Disconnected);
	UpdatePlaceholder();
}

ChatDock::~ChatDock()
{
	Disconnect();
}

void ChatDock::RegisterIcons()
{
	qreal dpr = devicePixelRatioF();
	QTextDocument *doc = view_->document();
	doc->addResource(QTextDocument::ImageResource, QUrl(QString::fromUtf8(kTwitchIconResource)),
			 PlatformImage(Platform::Twitch, iconSize_, dpr));
	doc->addResource(QTextDocument::ImageResource, QUrl(QString::fromUtf8(kYouTubeIconResource)),
			 PlatformImage(Platform::YouTube, iconSize_, dpr));
	doc->addResource(QTextDocument::ImageResource, QUrl(QString::fromUtf8(kSelfBadgeResource)),
			 SelfBadgeImage(iconSize_, dpr));
}

void ChatDock::Start()
{
	if (started_)
		return;
	started_ = true;
	obsStreaming_ = obs_frontend_streaming_active(); // the plugin may load while already live
	historyCutoff_ = (int64_t)std::time(nullptr) - kHistoryGraceSeconds;
	LoadConfig();
	Connect();
}

void ChatDock::Shutdown()
{
	if (!started_)
		return;
	Disconnect();
	SaveConfig();
	started_ = false;
}

void ChatDock::LoadConfig()
{
	char *path = obs_module_config_path("config.json");
	char *text = path ? os_quick_read_utf8_file(path) : nullptr;
	config_ = ParseConfig(text ? text : "");
	bfree(text);
	bfree(path);

	target_->SetTarget(config_.sendTarget);
	view_->document()->setMaximumBlockCount(config_.maxMessages);
	botMerger_.SetBots(config_.mergeBots);
	UpdatePlaceholder();
}

void ChatDock::SaveConfig()
{
	char *dir = obs_module_config_path("");
	char *path = obs_module_config_path("config.json");
	if (dir && path) {
		os_mkdirs(dir);
		std::string text = SerializeConfig(config_);
		if (!os_quick_write_utf8_file_safe(path, text.c_str(), text.size(), false, "tmp", "bak"))
			obs_log(LOG_WARNING, "failed to save %s", path);
	}
	bfree(path);
	bfree(dir);
}

ConnectionCallbacks ChatDock::MakeCallbacks(Platform platform)
{
	ConnectionCallbacks callbacks;
	callbacks.onMessages = [this](std::vector<ChatMessage> messages) {
		QMetaObject::invokeMethod(
			this, [this, messages = std::move(messages)]() { AppendMessages(messages); },
			Qt::QueuedConnection);
	};
	callbacks.onNotice = [this](const std::string &text) {
		obs_log(LOG_INFO, "%s", text.c_str());
		QString qtext = QString::fromStdString(text);
		QMetaObject::invokeMethod(this, [this, qtext]() { AppendNotice(qtext); }, Qt::QueuedConnection);
	};
	callbacks.onState = [this, platform](LinkState state) {
		QMetaObject::invokeMethod(
			this, [this, platform, state]() { SetLinkState(platform, state); }, Qt::QueuedConnection);
	};
	callbacks.onTokenChanged = [this, platform](const oauth::Token &token, const std::string &login) {
		QMetaObject::invokeMethod(
			this,
			[this, platform, token, login]() {
				if (platform == Platform::Twitch) {
					config_.twitchToken = token;
					if (!login.empty())
						config_.twitchLogin = login;
				} else {
					config_.youtubeToken = token;
				}
				SaveConfig();
			},
			Qt::QueuedConnection);
	};
	callbacks.onSendFailed = [this, platform](uint64_t sendId) {
		QMetaObject::invokeMethod(
			this, [this, platform, sendId]() { AppendLines(merger_.Fail(sendId, platform)); },
			Qt::QueuedConnection);
	};
	return callbacks;
}

void ChatDock::Connect()
{
	Disconnect();
	twitch_ = std::make_unique<TwitchConnection>(config_.twitchChannel, config_.twitchClientId, config_.twitchLogin,
						     config_.twitchToken, MakeCallbacks(Platform::Twitch));
	ConnectYouTube();
}

void ChatDock::ConnectYouTube()
{
	youtube_.reset();
	// Waiting for OBS's Start Streaming uses no quota. Signed out, the connection is still created so it can
	// say so; it makes no requests.
	if (config_.youtubeConnectOnStream && !obsStreaming_ && config_.youtubeToken.IsValid()) {
		AppendNotice(Text("Notice.YouTubeStandby"));
		// Queued, so it lands after any state the old connection had already posted.
		QMetaObject::invokeMethod(
			this, [this]() { SetLinkState(Platform::YouTube, LinkState::Standby); }, Qt::QueuedConnection);
		return;
	}
	youtube_ = std::make_unique<YouTubeConnection>(config_.youtubeClientId, config_.youtubeClientSecret,
						       config_.youtubeToken, config_.youtubeVideo,
						       config_.youtubePollSeconds, config_.youtubeStream,
						       config_.youtubeConnectOnStream, historyCutoff_,
						       MakeCallbacks(Platform::YouTube));
}

void ChatDock::OnStreamingChanged(bool streaming)
{
	if (obsStreaming_ == streaming)
		return;
	obsStreaming_ = streaming;
	if (streaming)
		historyCutoff_ =
			(int64_t)std::time(nullptr) - kHistoryGraceSeconds; // chat before going live is history
	if (started_ && config_.youtubeConnectOnStream)
		ConnectYouTube();
}

void ChatDock::Disconnect()
{
	twitch_.reset();
	youtube_.reset();
	SetLinkState(Platform::Twitch, LinkState::Disconnected);
	SetLinkState(Platform::YouTube, LinkState::Disconnected);
}

void ChatDock::OpenSettings()
{
	const ChatConfig before = config_;
	SettingsDialog dialog(config_, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	// Tokens may have been refreshed in the background while the dialog was open. Twitch refresh
	// tokens are single-use, so keep the live ones unless the user signed in or out in the dialog.
	ChatConfig result = dialog.Result();
	if (result.twitchToken.accessToken == before.twitchToken.accessToken) {
		result.twitchToken = config_.twitchToken;
		result.twitchLogin = config_.twitchLogin;
	}
	if (result.youtubeToken.accessToken == before.youtubeToken.accessToken)
		result.youtubeToken = config_.youtubeToken;
	config_ = result;
	view_->document()->setMaximumBlockCount(config_.maxMessages);
	botMerger_.SetBots(config_.mergeBots);
	SaveConfig();
	if (started_) {
		// The dock already shows this chat up to now; a fresh connection shouldn't replay it.
		if (youtube_)
			historyCutoff_ = (int64_t)std::time(nullptr);
		Connect();
	}
}

void ChatDock::UpdatePlaceholder()
{
	switch (target_->Target()) {
	case SendTarget::Twitch:
		input_->setPlaceholderText(Text("Input.Twitch"));
		break;
	case SendTarget::YouTube:
		input_->setPlaceholderText(Text("Input.YouTube"));
		break;
	default:
		input_->setPlaceholderText(Text("Input.Both"));
	}
}

void ChatDock::SendCurrent()
{
	auto plan = PlanSend(target_->Target(), input_->text().toStdString(), twitchState_ == LinkState::Connected,
			     youtubeState_ == LinkState::Connected);
	for (const auto &error : plan.errors)
		AppendNotice(QString::fromStdString(error));
	if (plan.blocked)
		return;

	// Your own message comes back from each platform; the merger turns those echoes into one line.
	uint64_t sendId = merger_.Begin(plan.targets, QDateTime::currentMSecsSinceEpoch());
	ScheduleEchoTimer();
	for (Platform platform : plan.targets) {
		if (platform == Platform::Twitch && twitch_)
			twitch_->Send(plan.text, sendId);
		else if (platform == Platform::YouTube && youtube_)
			youtube_->Send(plan.text, sendId);
		else
			AppendLines(merger_.Fail(sendId, platform));
	}
	input_->clear();
}

void ChatDock::AppendHtml(const QStringList &lines, const std::vector<int> &lineIds)
{
	if (lines.isEmpty())
		return;

	QScrollBar *bar = view_->verticalScrollBar();
	const bool atBottom = bar->value() >= bar->maximum() - 4;

	// The document relayouts the changed range and trims to the maximum block count once, at endEditBlock.
	QTextCursor cursor(view_->document());
	cursor.movePosition(QTextCursor::End);
	cursor.beginEditBlock();
	for (qsizetype i = 0; i < lines.size(); ++i) {
		if (!empty_)
			cursor.insertBlock();
		cursor.insertHtml(lines[i]);
		if ((size_t)i < lineIds.size())
			cursor.block().setUserState(lineIds[(size_t)i]);
		empty_ = false;
	}
	cursor.endEditBlock();

	if (atBottom)
		bar->setValue(bar->maximum());
}

bool ChatDock::ReplaceLine(int lineId, const QString &html)
{
	// Only runs when a bot's twin arrives; the line is normally among the last few.
	for (QTextBlock block = view_->document()->lastBlock(); block.isValid(); block = block.previous()) {
		if (block.userState() != lineId)
			continue;
		QTextCursor cursor(block);
		cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
		cursor.insertHtml(html);
		return true;
	}
	return false; // already trimmed by the message limit
}

void ChatDock::AppendMessages(const std::vector<ChatMessage> &messages)
{
	std::vector<DisplayLine> lines;
	lines.reserve(messages.size());
	for (const auto &message : messages) {
		auto released = merger_.Offer(message);
		std::move(released.begin(), released.end(), std::back_inserter(lines));
	}
	AppendLines(lines);
}

void ChatDock::ScheduleEchoTimer()
{
	auto deadline = merger_.NextDeadline();
	if (!deadline) {
		echoTimer_->stop();
		return;
	}
	const int64_t waitMs = *deadline - QDateTime::currentMSecsSinceEpoch();
	echoTimer_->start((int)std::clamp<int64_t>(waitMs, 0, 60000));
}

void ChatDock::AppendLines(const std::vector<DisplayLine> &lines)
{
	if (lines.empty()) {
		ScheduleEchoTimer();
		return;
	}
	if (backgroundStale_) {
		backgroundStale_ = false;
		view_->ensurePolished();
		const std::string previous = nameColors_.Background();
		nameColors_.SetBackground(view_->palette().color(QPalette::Base).name().toStdString());
		if (nameColors_.Background() != previous)
			obs_log(LOG_INFO, "name colors: chat background is %s", nameColors_.Background().c_str());
	}
	auto format = [this](const DisplayLine &line) {
		return QString::fromStdString(FormatMessageHtml(line.message, iconSize_, line.platforms, &nameColors_));
	};

	const int64_t now = QDateTime::currentMSecsSinceEpoch();
	QStringList html;
	std::vector<int> lineIds;
	html.reserve((qsizetype)lines.size());
	lineIds.reserve(lines.size());
	for (const auto &line : lines) {
		int lineId = -1;
		if (line.platforms.size() == 1 && botMerger_.IsBot(line.message.author)) {
			// A bot's copy of a line already shown for the other platform: add the icon to that line.
			if (auto merged = botMerger_.Match(line.message, now)) {
				QString mergedHtml = format(merged->line);
				if (ReplaceLine(merged->lineId, mergedHtml))
					continue;
				html.append(mergedHtml);
				lineIds.push_back(-1);
				continue;
			}
			lineId = nextLineId_;
			nextLineId_ = nextLineId_ == (std::numeric_limits<int>::max)() ? 1 : nextLineId_ + 1;
			botMerger_.Remember(line.message, lineId, now);
		}
		html.append(format(line));
		lineIds.push_back(lineId);
	}
	AppendHtml(html, lineIds);
	ScheduleEchoTimer();
}

void ChatDock::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
		backgroundStale_ = true;
	QWidget::changeEvent(event);
}

void ChatDock::AppendNotice(const QString &text)
{
	AppendHtml({QString::fromStdString(FormatNoticeHtml(text.toStdString()))});
}

void ChatDock::SetLinkState(Platform platform, LinkState state)
{
	QToolButton *button = platform == Platform::Twitch ? twitchStatus_ : youtubeStatus_;
	(platform == Platform::Twitch ? twitchState_ : youtubeState_) = state;
	button->setText(Text(StateKey(state)));
	button->setToolTip(QString::fromUtf8(PlatformName(platform).data(), (int)PlatformName(platform).size()) + ": " +
			   Text(StateKey(state)));
}

} // namespace unified_chat
