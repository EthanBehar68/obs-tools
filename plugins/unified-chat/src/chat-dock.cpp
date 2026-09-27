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
#include "secret-store.hpp"
#include "settings-dialog.hpp"
#include "target-switch.hpp"
#include "core/chat-format.hpp"
#include "core/moderation.hpp"
#include "core/text-util.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBlockUserData>
#include <QTextCharFormat>
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

struct ChatDock::LineData : QTextBlockUserData {
	explicit LineData(LineInfo lineInfo) : info(std::move(lineInfo)) {}
	LineInfo info;
};

// Allowance for clock differences between this PC and YouTube when deciding what counts as chat history.
static constexpr int64_t kHistoryGraceSeconds = 30;
static constexpr size_t kMaxSuggestions = 8;

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

	// Shown while a Twitch reply is being written.
	replyBar_ = new QWidget(this);
	auto replyLayout = new QHBoxLayout(replyBar_);
	replyLayout->setContentsMargins(0, 0, 0, 0);
	replyLabel_ = new QLabel(replyBar_);
	auto replyCancel = new QToolButton(replyBar_);
	replyCancel->setText(QStringLiteral("✕"));
	replyCancel->setAutoRaise(true);
	replyCancel->setToolTip(Text("Reply.Cancel"));
	replyLayout->addWidget(replyLabel_, 1);
	replyLayout->addWidget(replyCancel);
	replyBar_->hide();
	layout->addWidget(replyBar_);
	connect(replyCancel, &QToolButton::clicked, this, [this]() { SetReply(std::nullopt); });

	auto footer = new QHBoxLayout();
	input_ = new QLineEdit(this);
	input_->setMaxLength(500);
	target_ = new TargetSwitch(this);
	footer->addWidget(input_, 1);
	footer->addWidget(target_);
	layout->addLayout(footer);

	iconSize_ = qMax(14, fontMetrics().height());
	emoteHeight_ = qRound(iconSize_ * 1.5); // a little larger than text, as chosen
	hiDpi_ = devicePixelRatioF() > 1.25;
	assets_ = std::make_unique<AssetLoader>();

	// A child of the dock rather than a popup window: it never takes keyboard focus, so the input's own key
	// handling (eventFilter) decides what Enter, Tab and the arrows do while it's open.
	suggestions_ = new QListWidget(this);
	suggestions_->setFocusPolicy(Qt::NoFocus);
	suggestions_->setIconSize(QSize(iconSize_, iconSize_));
	suggestions_->setUniformItemSizes(true);
	suggestions_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	suggestions_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	suggestions_->hide();
	connect(suggestions_, &QListWidget::itemClicked, this,
		[this](QListWidgetItem *item) { ApplySuggestion(suggestions_->row(item)); });

	echoTimer_ = new QTimer(this);
	echoTimer_->setSingleShot(true); // armed for the next merge deadline only, see ScheduleEchoTimer
	connect(echoTimer_, &QTimer::timeout, this,
		[this]() { AppendLines(merger_.Expire(QDateTime::currentMSecsSinceEpoch())); });
	RegisterIcons();

	connect(input_, &QLineEdit::returnPressed, this, &ChatDock::SendCurrent);
	input_->installEventFilter(this); // "@" + Tab completion
	connect(view_, &QTextBrowser::anchorClicked, this, &ChatDock::OnLinkClicked);
	view_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(view_, &QWidget::customContextMenuRequested, this, &ChatDock::ShowLineMenu);
	connect(target_, &TargetSwitch::TargetChanged, this, [this]() {
		// Picked by hand: this is the target now, even in the middle of a mention.
		mentionTarget_.Forget();
		config_.sendTarget = target_->Target();
		if (config_.sendTarget != SendTarget::Twitch)
			SetReply(std::nullopt);
		UpdatePlaceholder();
		SaveConfig();
	});
	connect(input_, &QLineEdit::textChanged, this, [this](const QString &text) {
		// Sent (the input is cleared) or the mention was deleted: back to the chosen target.
		if (text.isEmpty()) {
			SetReply(std::nullopt);
			RestoreTarget();
			HideSuggestions();
		}
	});
	// Only the user's own typing updates suggestions; text set by the dock (a picked name) doesn't reopen them.
	connect(input_, &QLineEdit::textEdited, this, [this]() { UpdateSuggestions(); });
	connect(input_, &QLineEdit::cursorPositionChanged, this, [this]() {
		if (suggestions_->isVisible()) // moved away from the "@word": follow or close
			UpdateSuggestions();
	});
	connect(settingsButton_, &QToolButton::clicked, this, &ChatDock::OpenSettings);
	connect(twitchStatus_, &QToolButton::clicked, this, &ChatDock::OpenSettings);
	connect(youtubeStatus_, &QToolButton::clicked, this, &ChatDock::OpenSettings);
	connect(clearButton_, &QToolButton::clicked, this, [this]() {
		view_->clear();
		empty_ = true;
		RegisterIcons();
		RegisterLoadedImages();
	});

	SetLinkState(Platform::Twitch, LinkState::Disconnected);
	SetLinkState(Platform::YouTube, LinkState::Disconnected);
	UpdatePlaceholder();
}

ChatDock::~ChatDock()
{
	assets_.reset(); // stop downloads before the members their callbacks refer to go away
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
	ParseReport report;
	config_ = ParseConfig(text ? text : "", PlatformSecretCodec(), &report);
	bfree(text);
	bfree(path);

	target_->SetTarget(config_.sendTarget);
	view_->document()->setMaximumBlockCount(config_.maxMessages);
	botMerger_.SetBots(config_.mergeBots);
	UpdateMentionNames();
	UpdatePlaceholder();

	if (report.unreadableSecrets) {
		obs_log(LOG_WARNING,
			"saved sign-ins could not be decrypted (config from another Windows account or PC?)");
		AppendNotice(Text("Notice.SecretsUnreadable"));
	}
	// An older config kept tokens in plain text: re-save encrypted now, and drop the plain-text backup.
	if (report.plaintextSecrets && PlatformSecretCodec()) {
		plaintextBackup_ = true;
		SaveConfig();
	}
}

void ChatDock::SaveConfig()
{
	char *dir = obs_module_config_path("");
	char *path = obs_module_config_path("config.json");
	if (dir && path) {
		os_mkdirs(dir);
		std::string text = SerializeConfig(config_, PlatformSecretCodec());
		if (!os_quick_write_utf8_file_safe(path, text.c_str(), text.size(), false, "tmp", "bak")) {
			obs_log(LOG_WARNING, "failed to save %s", path);
		} else if (plaintextBackup_) {
			// The safe write keeps the previous file as config.json.bak: the old plain-text one this once.
			std::string backup = std::string(path) + ".bak";
			os_unlink(backup.c_str());
			plaintextBackup_ = false;
			obs_log(LOG_INFO, "saved sign-ins are now encrypted for this Windows account");
		}
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
	callbacks.onViewers = [this, platform](int64_t viewers) {
		QMetaObject::invokeMethod(
			this, [this, platform, viewers]() { SetViewers(platform, viewers); }, Qt::QueuedConnection);
	};
	callbacks.onChannelId = [this](std::string channelId) {
		QMetaObject::invokeMethod(
			this, [this, channelId = std::move(channelId)]() { LoadChannelAssets(channelId); },
			Qt::QueuedConnection);
	};
	// Queued like messages, so it runs after the lines it refers to have been added.
	callbacks.onModeration = [this](ModerationEvent event) {
		QMetaObject::invokeMethod(
			this, [this, event = std::move(event)]() { ApplyModeration(event); }, Qt::QueuedConnection);
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
					if (!login.empty() && login != config_.twitchLogin) {
						config_.twitchLogin = login;
						UpdateMentionNames();
					}
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
	// Reload channel emotes and badges when the connection comes back (a sign-in may enable badges; the channel
	// may have changed). Global emote lists stay loaded.
	assetChannelId_.clear();
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
	UpdateMentionNames();
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
		if (platform == Platform::Twitch && twitch_) {
			if (reply_)
				twitch_->Send(plan.text, sendId, reply_->messageId, reply_->mention);
			else
				twitch_->Send(plan.text, sendId);
		} else if (platform == Platform::YouTube && youtube_) {
			youtube_->Send(plan.text, sendId);
		} else {
			AppendLines(merger_.Fail(sendId, platform));
		}
	}
	input_->clear();
	SetReply(std::nullopt);
}

void ChatDock::AppendHtml(const std::vector<HtmlLine> &lines)
{
	if (lines.empty())
		return;

	QScrollBar *bar = view_->verticalScrollBar();
	const bool atBottom = bar->value() >= bar->maximum() - 4;

	QTextBlockFormat highlighted;
	highlighted.setBackground(highlightColor_);

	// The document relayouts the changed range and trims to the maximum block count once, at endEditBlock.
	QTextCursor cursor(view_->document());
	cursor.movePosition(QTextCursor::End);
	cursor.beginEditBlock();
	for (const auto &line : lines) {
		if (!empty_)
			cursor.insertBlock();
		// A new block inherits the previous block's format, so set it every time.
		cursor.setBlockFormat(line.highlight ? highlighted : QTextBlockFormat());
		cursor.insertHtml(line.html);
		cursor.block().setUserState(line.lineId);
		if (line.info)
			cursor.block().setUserData(new LineData(*line.info));
		empty_ = false;
	}
	cursor.endEditBlock();

	if (atBottom)
		bar->setValue(bar->maximum());
}

bool ChatDock::ReplaceLine(int lineId, const QString &html, const DisplayLine &line, int textLength)
{
	// Only runs when a bot's twin arrives; the line is normally among the last few.
	for (QTextBlock block = view_->document()->lastBlock(); block.isValid(); block = block.previous()) {
		if (block.userState() != lineId)
			continue;
		QTextCursor cursor(block);
		cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
		cursor.insertHtml(html);
		block.setUserData(new LineData(MakeLineInfo(line, textLength))); // now on both platforms
		return true;
	}
	return false; // already trimmed by the message limit
}

ChatDock::LineInfo ChatDock::MakeLineInfo(const DisplayLine &line, int textLength)
{
	LineInfo info;
	for (Platform platform : line.platforms)
		(platform == Platform::Twitch ? info.twitch : info.youtube) = true;
	const ChatMessage &message = line.message;
	info.platform = message.platform;
	info.self = message.isSelf;
	info.messageId = message.id;
	info.login = ToLower(message.mention);
	info.authorId = message.authorId;
	info.author = message.author;
	info.textLength = textLength; // as rendered: an emote image is one character
	return info;
}

void ChatDock::StrikeLine(const QTextBlock &block, LineInfo &info, const ModerationTag &tag)
{
	if (info.severity >= tag.severity)
		return; // already marked at least this seriously (banned > timed out > deleted / cleared)

	QTextCursor cursor(block);
	int end = block.position() + block.length() - 1; // the text, then any tag, end the block
	if (info.tagLength > 0) {
		// A more serious tag replaces the old one; the text is already struck.
		cursor.setPosition(end - info.tagLength);
		cursor.setPosition(end, QTextCursor::KeepAnchor);
		cursor.removeSelectedText();
		end -= info.tagLength;
	} else {
		cursor.setPosition(end - info.textLength);
		cursor.setPosition(end, QTextCursor::KeepAnchor);
		QTextCharFormat struck;
		struck.setFontStrikeOut(true);
		struck.setForeground(QColor(QString::fromUtf8(kDimmedTextColor)));
		cursor.mergeCharFormat(struck);
	}

	QTextCharFormat tagFormat;
	tagFormat.setForeground(QColor(QString::fromStdString(tag.color)));
	tagFormat.setFontItalic(true);
	const QString label = QLatin1Char(' ') + QString::fromStdString(tag.label);
	cursor.setPosition(end);
	cursor.insertText(label, tagFormat);
	info.tagLength = (int)label.size();
	info.severity = tag.severity;
}

// A transparent stand-in for an image that hasn't arrived yet; the <img> size, not the image, sets the layout.
static const QImage &PlaceholderImage()
{
	static const QImage image = [] {
		QImage transparent(1, 1, QImage::Format_ARGB32_Premultiplied);
		transparent.fill(Qt::transparent);
		return transparent;
	}();
	return image;
}

std::vector<std::string> ChatDock::HelixHeaders() const
{
	if (!config_.twitchToken.IsValid() || config_.twitchClientId.empty())
		return {};
	return {"Authorization: Bearer " + config_.twitchToken.accessToken, "Client-Id: " + config_.twitchClientId};
}

void ChatDock::LoadChannelAssets(const std::string &channelId)
{
	if (channelId.empty() || channelId == assetChannelId_)
		return;
	assetChannelId_ = channelId;

	// Another channel: its emotes and badges replace the previous channel's; globals stay.
	emoteSets_.erase(std::remove_if(emoteSets_.begin(), emoteSets_.end(),
					[](const EmoteSet &set) { return set.channel; }),
			 emoteSets_.end());
	emotes_.Clear();
	for (const auto &set : emoteSets_)
		emotes_.Add(set.emotes, set.priority);
	channelBadges_.clear();

	using Parser = std::vector<Emote> (*)(const std::string &);
	auto fetchEmotes = [this, channelId](const std::string &url, EmoteProvider provider, bool channel,
					     Parser parse) {
		assets_->Get(url, {}, [this, channelId, provider, channel, parse](const HttpResponse &res) {
			if (!res.Ok())
				return;              // e.g. 404: the channel doesn't use this service
			auto list = parse(res.body); // parsed here, off the UI thread
			if (list.empty())
				return;
			QMetaObject::invokeMethod(
				this,
				[this, channelId, provider, channel, list = std::move(list)]() mutable {
					if (channel && channelId != assetChannelId_)
						return; // arrived after switching channels
					AddEmoteSet(std::move(list), provider, channel);
				},
				Qt::QueuedConnection);
		});
	};
	auto fetchBadges = [this, channelId](const std::string &url, bool channel) {
		auto headers = HelixHeaders();
		if (headers.empty())
			return; // the badge API needs a sign-in; read-only chat shows no badges
		assets_->Get(url, std::move(headers),
			     [this, channelId, channel, hiDpi = hiDpi_](const HttpResponse &res) {
				     if (!res.Ok())
					     return;
				     auto images = ParseBadgeImages(res.body, hiDpi);
				     QMetaObject::invokeMethod(
					     this,
					     [this, channelId, channel, images = std::move(images)]() mutable {
						     if (channel && channelId != assetChannelId_)
							     return;
						     (channel ? channelBadges_ : globalBadges_).merge(images);
						     // Lines already shown with a badge placeholder can load their image now.
						     static const std::string prefix = "unified-chat://badge/";
						     for (const auto &key : imageKeys_) {
							     if (key.rfind(prefix, 0) != 0)
								     continue;
							     if (const std::string *url =
									 BadgeUrl(key.substr(prefix.size())))
								     RequestImage(key, *url);
						     }
					     },
					     Qt::QueuedConnection);
			     });
	};

	if (!globalAssetsRequested_) {
		globalAssetsRequested_ = true;
		fetchEmotes("https://api.betterttv.net/3/cached/emotes/global", EmoteProvider::BTTV, false,
			    ParseBttvEmotes);
		fetchEmotes("https://api.frankerfacez.com/v1/set/global", EmoteProvider::FFZ, false, ParseFfzEmotes);
		fetchEmotes("https://7tv.io/v3/emote-sets/global", EmoteProvider::SevenTV, false, ParseSevenTvEmotes);
	}
	if (globalBadges_.empty())
		fetchBadges("https://api.twitch.tv/helix/chat/badges/global", false);
	fetchEmotes("https://api.betterttv.net/3/cached/users/twitch/" + channelId, EmoteProvider::BTTV, true,
		    ParseBttvEmotes);
	fetchEmotes("https://api.frankerfacez.com/v1/room/id/" + channelId, EmoteProvider::FFZ, true, ParseFfzEmotes);
	fetchEmotes("https://7tv.io/v3/users/twitch/" + channelId, EmoteProvider::SevenTV, true, ParseSevenTvEmotes);
	fetchBadges("https://api.twitch.tv/helix/chat/badges?broadcaster_id=" + channelId, true);
}

const std::string *ChatDock::BadgeUrl(const std::string &badgeKey) const
{
	// The channel's own badges (e.g. custom subscriber badges) take precedence over the global ones.
	if (auto channel = channelBadges_.find(badgeKey); channel != channelBadges_.end())
		return &channel->second;
	if (auto global = globalBadges_.find(badgeKey); global != globalBadges_.end())
		return &global->second;
	return nullptr;
}

void ChatDock::AddEmoteSet(std::vector<Emote> emotes, EmoteProvider provider, bool channel)
{
	const int priority = EmotePriority(provider, channel);
	emoteSets_.push_back({std::move(emotes), priority, channel});
	emotes_.Add(emoteSets_.back().emotes, priority); // priorities make arrival order irrelevant
	obs_log(LOG_INFO, "emotes: %zu %s emotes loaded", emoteSets_.back().emotes.size(),
		provider == EmoteProvider::SevenTV ? "7TV"
		: provider == EmoteProvider::BTTV  ? "BTTV"
						   : "FFZ");
}

void ChatDock::EnsureLineImages(const ChatMessage &message)
{
	auto ensure = [this](const std::string &key) {
		if (imageKeys_.insert(key).second)
			view_->document()->addResource(QTextDocument::ImageResource, QUrl(QString::fromStdString(key)),
						       PlaceholderImage());
	};

	for (const auto &badge : message.badges) {
		const std::string key = BadgeImageKey(badge);
		ensure(key);
		if (const std::string *url = BadgeUrl(BadgeKey(badge)))
			RequestImage(key, *url); // otherwise requested once the badge list arrives
	}

	if (message.platform != Platform::Twitch || (message.emotes.empty() && emotes_.Size() == 0))
		return;
	for (const auto &segment : SplitMessage(message.text, message.emotes, &emotes_)) {
		if (!segment.emote)
			continue;
		const std::string key = EmoteImageKey(*segment.emote);
		ensure(key);
		RequestImage(key, EmoteImageUrl(*segment.emote, hiDpi_));
	}
}

void ChatDock::RequestImage(const std::string &key, const std::string &url)
{
	if (!requested_.insert(key).second)
		return; // once per session
	assets_->Get(url, {}, [this, key](const HttpResponse &res) {
		if (!res.Ok())
			return;
		QImage image; // decoded here, off the UI thread (PNG, GIF first frame, WebP)
		if (!image.loadFromData(reinterpret_cast<const uchar *>(res.body.data()), (int)res.body.size()))
			return;
		QMetaObject::invokeMethod(
			this,
			[this, key, image = std::move(image)]() {
				images_[key] = image;
				view_->document()->addResource(QTextDocument::ImageResource,
							       QUrl(QString::fromStdString(key)), image);
				view_->viewport()->update(); // same size as the placeholder: a repaint, no relayout
			},
			Qt::QueuedConnection);
	});
}

void ChatDock::RegisterLoadedImages()
{
	for (const auto &key : imageKeys_) {
		auto image = images_.find(key);
		view_->document()->addResource(QTextDocument::ImageResource, QUrl(QString::fromStdString(key)),
					       image != images_.end() ? image->second : PlaceholderImage());
	}
}

void ChatDock::ShowLineMenu(const QPoint &pos)
{
	std::unique_ptr<QMenu> menu(view_->createStandardContextMenu(pos)); // Copy, Select All
	const QTextBlock block = view_->cursorForPosition(pos).block();
	const auto data = static_cast<const LineData *>(block.userData());

	// Someone else's chat line: add moderation. The info is copied because new chat may trim the line while
	// the menu (and later the confirmation) is open.
	if (data && !data->info.self && !data->info.authorId.empty()) {
		const LineInfo info = data->info;
		auto action = [info](ModerationAction::Kind kind, int64_t seconds = 0) {
			ModerationAction a;
			a.platform = info.platform;
			a.kind = kind;
			a.messageId = info.messageId;
			a.userId = info.authorId;
			a.userName = info.author;
			a.durationSeconds = seconds;
			return a;
		};
		auto add = [this](QMenu *target, const QString &label, ModerationAction a, bool customTimeout = false) {
			QAction *item = target->addAction(label);
			connect(item, &QAction::triggered, this, [this, a, customTimeout]() mutable {
				if (ConfirmModeration(a, customTimeout))
					RunModeration(a);
			});
			return item;
		};

		menu->addSeparator();
		const QString platform = QString::fromUtf8(PlatformName(info.platform).data());
		menu->addAction(QString::fromStdString(info.author) + " (" + platform + ")")->setEnabled(false);
		add(menu.get(), Text("Mod.Delete"), action(ModerationAction::Kind::DeleteMessage))
			->setEnabled(!info.messageId.empty());
		QMenu *timeout = menu->addMenu(Text("Mod.Timeout"));
		add(timeout, Text("Mod.Timeout1m"), action(ModerationAction::Kind::Timeout, 60));
		add(timeout, Text("Mod.Timeout10m"), action(ModerationAction::Kind::Timeout, 600));
		add(timeout, Text("Mod.Timeout1h"), action(ModerationAction::Kind::Timeout, 3600));
		add(timeout, Text("Mod.Timeout24h"), action(ModerationAction::Kind::Timeout, 86400));
		timeout->addSeparator();
		add(timeout, Text("Mod.TimeoutCustom"), action(ModerationAction::Kind::Timeout), true);
		add(menu.get(), Text("Mod.Ban"), action(ModerationAction::Kind::Ban));
		// Offered on lines already marked timed out or banned. YouTube can only lift bans made here.
		const bool liftable = info.platform == Platform::Twitch || youtubeDockBans_.count(info.authorId) > 0;
		if (info.severity >= 2 && liftable)
			add(menu.get(), Text("Mod.Unban"), action(ModerationAction::Kind::Unban));
	}
	menu->exec(view_->viewport()->mapToGlobal(pos));
}

bool ChatDock::ConfirmModeration(ModerationAction &action, bool customTimeout)
{
	const QString name = QString::fromStdString(action.userName);
	const QString platform = QString::fromUtf8(PlatformName(action.platform).data());
	const QString quota = action.platform == Platform::YouTube ? QStringLiteral("\n\n") + Text("Mod.Quota")
								   : QString();
	auto ask = [&](const QString &question) {
		return QMessageBox::question(this, Text("Mod.Title"), question + quota) == QMessageBox::Yes;
	};

	switch (action.kind) {
	case ModerationAction::Kind::DeleteMessage:
		return ask(Text("Mod.ConfirmDelete").arg(name, platform));
	case ModerationAction::Kind::Timeout: {
		if (customTimeout) {
			// The input dialog is the confirmation: OK times out, Cancel doesn't.
			bool ok = false;
			const int minutes = QInputDialog::getInt(this, Text("Mod.Title"),
								 Text("Mod.AskMinutes").arg(name, platform) + quota, 10,
								 1, (int)(kTwitchMaxTimeoutSeconds / 60), 1, &ok);
			if (!ok)
				return false;
			action.durationSeconds = (int64_t)minutes * 60;
			return true;
		}
		return ask(
			Text("Mod.ConfirmTimeout")
				.arg(name, platform, QString::fromStdString(FormatDuration(action.durationSeconds))));
	}
	case ModerationAction::Kind::Ban: {
		// A permanent ban asks with an optional reason (Twitch keeps it; YouTube's API has no reason field).
		QDialog dialog(this);
		dialog.setWindowTitle(Text("Mod.Title"));
		auto layout = new QVBoxLayout(&dialog);
		auto question = new QLabel(Text("Mod.ConfirmBan").arg(name, platform) + quota, &dialog);
		question->setWordWrap(true);
		layout->addWidget(question);
		QLineEdit *reason = nullptr;
		if (action.platform == Platform::Twitch) {
			reason = new QLineEdit(&dialog);
			reason->setPlaceholderText(Text("Mod.Reason"));
			reason->setMaxLength(500);
			layout->addWidget(reason);
		}
		auto buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &dialog);
		connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);
		if (dialog.exec() != QDialog::Accepted)
			return false;
		if (reason)
			action.reason = reason->text().trimmed().toStdString();
		return true;
	}
	case ModerationAction::Kind::Unban:
		return ask(Text("Mod.ConfirmUnban").arg(name, platform));
	}
	return false;
}

void ChatDock::RunModeration(const ModerationAction &action)
{
	if (action.platform == Platform::Twitch && twitch_) {
		twitch_->Moderate(action);
		return;
	}
	if (action.platform == Platform::YouTube && youtube_) {
		if (action.kind == ModerationAction::Kind::Ban || action.kind == ModerationAction::Kind::Timeout)
			youtubeDockBans_.insert(action.userId);
		else if (action.kind == ModerationAction::Kind::Unban)
			youtubeDockBans_.erase(action.userId);
		youtube_->Moderate(action);
		return;
	}
	AppendNotice(QString::fromUtf8(PlatformName(action.platform).data()) + ": " + Text("Mod.NotConnected"));
}

void ChatDock::ApplyModeration(const ModerationEvent &event)
{
	const ModerationTag tag = TagFor(event);
	const std::string login = ToLower(event.userLogin);
	std::string author;

	// Rare, so a walk back over the kept lines is cheap enough.
	QTextCursor edit(view_->document());
	edit.beginEditBlock();
	for (QTextBlock block = view_->document()->lastBlock(); block.isValid(); block = block.previous()) {
		auto data = static_cast<LineData *>(block.userData());
		if (!data)
			continue; // a notice
		LineInfo &info = data->info;
		if (!(event.platform == Platform::Twitch ? info.twitch : info.youtube))
			continue;

		bool match = false;
		switch (event.kind) {
		case ModerationEvent::Kind::DeleteMessage:
			match = !event.messageId.empty() && info.messageId == event.messageId;
			break;
		case ModerationEvent::Kind::RemoveUser:
			match = (!event.userId.empty() && info.authorId == event.userId) ||
				(!login.empty() && info.login == login);
			break;
		case ModerationEvent::Kind::ClearChat:
			match = true;
			break;
		}
		if (!match)
			continue;
		if (author.empty())
			author = info.author;
		StrikeLine(block, info, tag);
		if (event.kind == ModerationEvent::Kind::DeleteMessage)
			break;
	}
	edit.endEditBlock();

	std::string name = !event.userName.empty() && event.platform == Platform::YouTube ? event.userName : author;
	if (name.empty())
		name = event.userName;
	const std::string notice = ModerationNotice(event, name);
	if (!notice.empty())
		AppendNotice(QString::fromStdString(notice));
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
		const QColor base = view_->palette().color(QPalette::Base);
		const std::string previous = nameColors_.Background();
		nameColors_.SetBackground(base.name().toStdString());
		if (nameColors_.Background() != previous)
			obs_log(LOG_INFO, "name colors: chat background is %s", nameColors_.Background().c_str());
		// A step lighter on dark themes, darker on light ones: visible, but names stay readable.
		highlightColor_ = base.lightness() < 128 ? base.lighter(140) : base.darker(112);
	}
	auto format = [this](const DisplayLine &line, int &textLength) {
		EnsureLineImages(line.message); // placeholders first, so nothing shows as a broken image
		return QString::fromStdString(FormatMessageHtml(line.message, iconSize_, line.platforms, &nameColors_,
								&emotes_, emoteHeight_, &textLength));
	};

	const int64_t now = QDateTime::currentMSecsSinceEpoch();
	std::vector<HtmlLine> html;
	html.reserve(lines.size());
	for (const auto &line : lines) {
		const ChatMessage &message = line.message;
		if (message.isSelf)
			LearnOwnName(message);
		else
			chatters_.Add(message.platform, message.mention);
		const bool highlight = !message.isSelf && !mentions_.Empty() && mentions_.Matches(message.text);

		int lineId = -1;
		if (line.platforms.size() == 1 && botMerger_.IsBot(message.author)) {
			// A bot's copy of a line already shown for the other platform: add the icon to that line.
			if (auto merged = botMerger_.Match(message, now)) {
				int mergedLength = 0;
				QString mergedHtml = format(merged->line, mergedLength);
				if (!ReplaceLine(merged->lineId, mergedHtml, merged->line, mergedLength))
					html.push_back(
						{mergedHtml, -1, highlight, MakeLineInfo(merged->line, mergedLength)});
				continue;
			}
			lineId = nextLineId_;
			nextLineId_ = nextLineId_ == (std::numeric_limits<int>::max)() ? 1 : nextLineId_ + 1;
			botMerger_.Remember(message, lineId, now);
		}
		int textLength = 0;
		QString lineHtml = format(line, textLength);
		html.push_back({std::move(lineHtml), lineId, highlight, MakeLineInfo(line, textLength)});
	}
	AppendHtml(html);
	ScheduleEchoTimer();
}

void ChatDock::LearnOwnName(const ChatMessage &message)
{
	// Your YouTube handle/name isn't configured anywhere; your own messages reveal it (a sent echo may carry
	// the channel title, a polled one the handle), so keep a few spellings for mention highlighting.
	if (message.platform != Platform::YouTube || message.mention.empty())
		return;
	if (std::find(ownYouTubeNames_.begin(), ownYouTubeNames_.end(), message.mention) != ownYouTubeNames_.end())
		return;
	if (ownYouTubeNames_.size() >= 4)
		ownYouTubeNames_.erase(ownYouTubeNames_.begin());
	ownYouTubeNames_.push_back(message.mention);
	UpdateMentionNames();
}

void ChatDock::UpdateMentionNames()
{
	std::vector<std::string> names = ownYouTubeNames_;
	if (!config_.twitchLogin.empty())
		names.push_back(config_.twitchLogin);
	mentions_.SetNames(names);
}

void ChatDock::SwitchForMention(Platform platform)
{
	// Temporary: config_.sendTarget (the saved choice) stays as it is and comes back after sending.
	const SendTarget target = mentionTarget_.Switch(target_->Target(), platform);
	if (target_->Target() != target) {
		target_->SetTarget(target);
		UpdatePlaceholder();
	}
	if (target != SendTarget::Twitch)
		SetReply(std::nullopt);
}

void ChatDock::RestoreTarget()
{
	auto home = mentionTarget_.Restore();
	if (!home || target_->Target() == *home)
		return;
	target_->SetTarget(*home);
	if (*home != SendTarget::Twitch)
		SetReply(std::nullopt);
	UpdatePlaceholder();
}

void ChatDock::SetReply(std::optional<PendingReply> reply)
{
	reply_ = std::move(reply);
	if (reply_)
		replyLabel_->setText(Text("Reply.To").arg(QString::fromStdString(reply_->mention)));
	replyBar_->setVisible(reply_.has_value());
}

void ChatDock::OnLinkClicked(const QUrl &url)
{
	auto link = ParseMentionLink(url.toString(QUrl::FullyEncoded).toStdString());
	if (!link)
		return;

	// Talk to them where they are: a Twitch name must never go to YouTube and vice versa.
	SwitchForMention(link->platform);

	const QString mention = QStringLiteral("@") + QString::fromStdString(link->mention) + QLatin1Char(' ');
	QString text = input_->text();
	if (!text.contains(mention.trimmed(), Qt::CaseInsensitive)) {
		if (!text.isEmpty() && !text.endsWith(QLatin1Char(' ')))
			text += QLatin1Char(' ');
		text = text.isEmpty() ? mention : text + mention;
		input_->setText(text);
	}
	input_->setCursorPosition((int)input_->text().size());

	// Twitch can thread the answer under that message; YouTube has no replies.
	if (link->platform == Platform::Twitch && !link->messageId.empty())
		SetReply(PendingReply{link->messageId, link->mention});
	else
		SetReply(std::nullopt);
	input_->setFocus();
}

std::optional<Platform> ChatDock::MentionCandidatesPlatform() const
{
	// With Both chosen, offer everyone (each pick switches platform); otherwise the current platform only.
	if (mentionTarget_.Home(target_->Target()) == SendTarget::Both)
		return std::nullopt;
	return target_->Target() == SendTarget::YouTube ? Platform::YouTube : Platform::Twitch;
}

bool ChatDock::CurrentMentionWord(int &start, int &end, QString &prefix) const
{
	const QString text = input_->text();
	end = input_->cursorPosition();
	start = end;
	while (start > 0 && !text[start - 1].isSpace())
		--start;
	if (start >= text.size() || text[start] != QLatin1Char('@'))
		return false;
	prefix = text.mid(start + 1, end - start - 1);
	return true;
}

void ChatDock::UpdateSuggestions()
{
	int start, end;
	QString prefix;
	if (!CurrentMentionWord(start, end, prefix) || prefix.isEmpty()) {
		HideSuggestions();
		return;
	}
	auto matches = chatters_.Complete(prefix.toStdString(), MentionCandidatesPlatform(), kMaxSuggestions);
	if (matches.empty()) {
		HideSuggestions();
		return;
	}

	suggestions_->clear();
	for (const auto &match : matches) {
		auto item = new QListWidgetItem(PlatformIcon(match.platform), QString::fromStdString(match.mention));
		item->setData(Qt::UserRole, (int)match.platform);
		suggestions_->addItem(item);
	}
	suggestions_->setCurrentRow(0);

	// Just above the input, over the bottom of the chat view.
	const int rowHeight = qMax(suggestions_->sizeHintForRow(0), iconSize_ + 4);
	const int height = rowHeight * suggestions_->count() + 2 * suggestions_->frameWidth();
	const int width = qMin(input_->width(), qMax(200, input_->width() / 2));
	const QPoint inputTopLeft = input_->mapTo(this, QPoint(0, 0));
	suggestions_->setGeometry(inputTopLeft.x(), inputTopLeft.y() - height, width, height);
	suggestions_->raise();
	suggestions_->show();
}

void ChatDock::ApplySuggestion(int row)
{
	QListWidgetItem *item = suggestions_->item(row);
	int start, end;
	QString prefix;
	if (!item || !CurrentMentionWord(start, end, prefix)) {
		HideSuggestions();
		return;
	}
	const auto platform = (Platform)item->data(Qt::UserRole).toInt();
	const QString text = input_->text();
	const QString replacement = QLatin1Char('@') + item->text() + QLatin1Char(' ');
	input_->setText(text.left(start) + replacement + text.mid(end));
	input_->setCursorPosition(start + (int)replacement.size());
	HideSuggestions();
	// Like clicking the name in chat: send to that person's platform, just for this message.
	SwitchForMention(platform);
}

void ChatDock::HideSuggestions()
{
	if (suggestions_->isVisible())
		suggestions_->hide();
}

bool ChatDock::CompleteMention()
{
	const QString text = input_->text();
	const int cursor = input_->cursorPosition();
	const bool continuing = !completions_.empty() && cursor == completionEnd_;

	if (!continuing) {
		int start, end;
		QString prefix;
		if (!CurrentMentionWord(start, end, prefix))
			return false;
		completions_ = chatters_.Complete(prefix.toStdString(), MentionCandidatesPlatform());
		if (completions_.empty())
			return true; // an "@word" with no match: keep focus in the input
		completionIndex_ = 0;
		completionStart_ = start;
		completionEnd_ = cursor;
	} else {
		completionIndex_ = (completionIndex_ + 1) % completions_.size();
	}

	const auto &entry = completions_[completionIndex_];
	const QString replacement = QStringLiteral("@") + QString::fromStdString(entry.mention) + QLatin1Char(' ');
	input_->setText(text.left(completionStart_) + replacement + text.mid(completionEnd_));
	completionEnd_ = completionStart_ + (int)replacement.size();
	input_->setCursorPosition(completionEnd_);

	// With Both chosen, every name shown while cycling switches to that person's platform. This checks the
	// chosen target, not the current one, which the previous name in the cycle may already have switched.
	if (mentionTarget_.Home(target_->Target()) == SendTarget::Both)
		SwitchForMention(entry.platform);
	return true;
}

bool ChatDock::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == input_ && event->type() == QEvent::KeyPress) {
		auto key = static_cast<QKeyEvent *>(event);
		// While the suggestion list is open, these keys drive it instead of the input (Enter picks, not sends).
		if (suggestions_->isVisible()) {
			switch (key->key()) {
			case Qt::Key_Up:
				suggestions_->setCurrentRow(qMax(0, suggestions_->currentRow() - 1));
				return true;
			case Qt::Key_Down:
				suggestions_->setCurrentRow(
					qMin(suggestions_->count() - 1, suggestions_->currentRow() + 1));
				return true;
			case Qt::Key_Return:
			case Qt::Key_Enter:
			case Qt::Key_Tab:
				ApplySuggestion(suggestions_->currentRow());
				return true;
			case Qt::Key_Escape:
				HideSuggestions();
				return true;
			default:
				break;
			}
		}
		if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier)
			return CompleteMention() || QWidget::eventFilter(watched, event);
		completions_.clear(); // any other key ends a completion cycle
	} else if (watched == input_ && event->type() == QEvent::FocusOut) {
		HideSuggestions();
	}
	return QWidget::eventFilter(watched, event);
}

void ChatDock::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
		backgroundStale_ = true;
	QWidget::changeEvent(event);
}

void ChatDock::AppendNotice(const QString &text)
{
	AppendHtml({{QString::fromStdString(FormatNoticeHtml(text.toStdString()))}});
}

void ChatDock::SetLinkState(Platform platform, LinkState state)
{
	(platform == Platform::Twitch ? twitchState_ : youtubeState_) = state;
	// A count only means something while connected to a live chat.
	if (state != LinkState::Connected && state != LinkState::ReadOnly)
		(platform == Platform::Twitch ? twitchViewers_ : youtubeViewers_) = -1;
	UpdateStatusButtons();
}

void ChatDock::SetViewers(Platform platform, int64_t viewers)
{
	(platform == Platform::Twitch ? twitchViewers_ : youtubeViewers_) = viewers;
	UpdateStatusButtons();
}

void ChatDock::UpdateStatusButtons()
{
	auto viewersText = [](int64_t viewers) {
		return Text("Status.Viewers").arg(QLocale().toString((qlonglong)viewers));
	};
	const int64_t total = (twitchViewers_ > 0 ? twitchViewers_ : 0) + (youtubeViewers_ > 0 ? youtubeViewers_ : 0);

	for (Platform platform : {Platform::Twitch, Platform::YouTube}) {
		QToolButton *button = platform == Platform::Twitch ? twitchStatus_ : youtubeStatus_;
		const LinkState state = platform == Platform::Twitch ? twitchState_ : youtubeState_;
		const int64_t viewers = platform == Platform::Twitch ? twitchViewers_ : youtubeViewers_;
		QString text = Text(StateKey(state));
		if (viewers >= 0)
			text += QStringLiteral("  \U0001F441 ") + QLocale().toString((qlonglong)viewers);
		button->setText(text);

		QString tip = QString::fromUtf8(PlatformName(platform).data(), (int)PlatformName(platform).size()) +
			      ": " + Text(StateKey(state));
		if (viewers >= 0)
			tip += ", " + viewersText(viewers);
		if (twitchViewers_ >= 0 && youtubeViewers_ >= 0)
			tip += "\n" + Text("Status.TotalViewers").arg(QLocale().toString((qlonglong)total));
		button->setToolTip(tip);
	}
}

} // namespace unified_chat
