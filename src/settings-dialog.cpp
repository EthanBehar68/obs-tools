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

#include "settings-dialog.hpp"
#include "platform-icons.hpp"
#include "core/chat-format.hpp"
#include "core/text-util.hpp"
#include "core/twitch-irc.hpp"

#include <obs-module.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace unified_chat {

static QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static QString FromStd(const std::string &value)
{
	return QString::fromStdString(value);
}

static std::string ToStd(const QString &value)
{
	return Trim(value.toStdString());
}

static QPushButton *MakeButton(const char *key, QWidget *parent)
{
	return new QPushButton(Text(key), parent);
}

// What the marks in the chat view mean: one row per mark, a sample on the left and its meaning on the right.
static QWidget *MakeLegend(QWidget *parent)
{
	auto box = new QWidget(parent);
	auto grid = new QGridLayout(box);
	grid->setColumnStretch(1, 1);
	grid->setHorizontalSpacing(12);
	int row = 0;

	auto addRow = [&](QWidget *sample, const char *meaningKey) {
		auto meaning = new QLabel(Text(meaningKey), box);
		meaning->setWordWrap(true);
		grid->addWidget(sample, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
		grid->addWidget(meaning, row, 1);
		++row;
	};
	auto image = [&](const QImage &picture) {
		auto label = new QLabel(box);
		label->setPixmap(QPixmap::fromImage(picture));
		return label;
	};
	auto html = [&](const QString &text) {
		auto label = new QLabel(text, box);
		label->setTextFormat(Qt::RichText);
		return label;
	};
	const qreal dpr = parent ? parent->devicePixelRatioF() : 1.0;
	const int size = 16;
	auto grey = [](const QString &text) {
		return QStringLiteral("<span style='color:#9a9a9a;'>") + text + "</span>";
	};
	auto struck = [](const char *tagColor, const QString &tag) {
		return QStringLiteral("<s style='color:%1;'>message</s> <i style='color:%2;'>%3</i>")
			.arg(QString::fromUtf8(kDimmedTextColor), QString::fromUtf8(tagColor), tag);
	};

	addRow(image(PlatformImage(Platform::Twitch, size, dpr)), "Legend.Twitch");
	addRow(image(PlatformImage(Platform::YouTube, size, dpr)), "Legend.YouTube");
	addRow(image(SelfBadgeImage(size, dpr)), "Legend.Self");
	addRow(html(grey(QStringLiteral("badges"))), "Legend.Badges");
	addRow(html(grey(QStringLiteral("emotes"))), "Legend.Emotes");
	addRow(html(QStringLiteral("<b>name</b>")), "Legend.Name");
	addRow(html(grey(QStringLiteral("→ @name"))), "Legend.Reply");

	auto mention = html(QStringLiteral("&nbsp;@you&nbsp;"));
	mention->setAutoFillBackground(true);
	QPalette palette = mention->palette();
	const QColor base = palette.color(QPalette::Base);
	palette.setColor(QPalette::Window, base.lightness() < 128 ? base.lighter(140) : base.darker(112));
	mention->setPalette(palette);
	addRow(mention, "Legend.Mention");

	addRow(html(struck(kTagGreyColor, QStringLiteral("(deleted)"))), "Legend.Deleted");
	addRow(html(struck(kTagTimeoutColor, QStringLiteral("(timed out …)"))), "Legend.TimedOut");
	addRow(html(struck(kTagBanColor, QStringLiteral("(banned)"))), "Legend.Banned");
	addRow(html(struck(kTagGreyColor, QStringLiteral("(chat cleared)"))), "Legend.Cleared");
	addRow(html(QStringLiteral("<i>") + grey(QStringLiteral("notice")) + "</i>"), "Legend.Notice");
	return box;
}

SettingsDialog::SettingsDialog(const ChatConfig &config, QWidget *parent) : QDialog(parent), config_(config)
{
	setWindowTitle(Text("Settings.Title"));
	setMinimumWidth(460);

	auto layout = new QVBoxLayout(this);
	// One page per area keeps the dialog short.
	auto tabs = new QTabWidget(this);
	layout->addWidget(tabs);

	// Twitch
	auto twitchBox = new QWidget(tabs);
	auto twitchForm = new QFormLayout(twitchBox);
	twitchChannel_ = new QLineEdit(FromStd(config.twitchChannel), twitchBox);
	twitchChannel_->setPlaceholderText(Text("Settings.Twitch.ChannelHint"));
	twitchClientId_ = new QLineEdit(FromStd(config.twitchClientId), twitchBox);
	twitchClientId_->setPlaceholderText(Text("Settings.ClientIdHint"));
	twitchAccount_ = new QLabel(twitchBox);
	twitchAccount_->setWordWrap(true);
	twitchAccount_->setTextInteractionFlags(Qt::TextBrowserInteraction);
	twitchAccount_->setOpenExternalLinks(true);
	twitchSignIn_ = MakeButton("Settings.SignIn", twitchBox);
	twitchSignOut_ = MakeButton("Settings.SignOut", twitchBox);
	auto twitchButtons = new QHBoxLayout();
	twitchButtons->addWidget(twitchSignIn_);
	twitchButtons->addWidget(twitchSignOut_);
	twitchButtons->addStretch();
	twitchForm->addRow(Text("Settings.Twitch.Channel"), twitchChannel_);
	twitchForm->addRow(Text("Settings.ClientId"), twitchClientId_);
	twitchForm->addRow(Text("Settings.Account"), twitchAccount_);
	twitchForm->addRow(QString(), twitchButtons);
	tabs->addTab(twitchBox, PlatformIcon(Platform::Twitch), Text("Settings.Twitch"));

	// YouTube
	auto youtubeBox = new QWidget(tabs);
	auto youtubeForm = new QFormLayout(youtubeBox);
	youtubeClientId_ = new QLineEdit(FromStd(config.youtubeClientId), youtubeBox);
	youtubeClientId_->setPlaceholderText(Text("Settings.ClientIdHint"));
	youtubeClientSecret_ = new QLineEdit(FromStd(config.youtubeClientSecret), youtubeBox);
	youtubeClientSecret_->setEchoMode(QLineEdit::Password);
	youtubeVideo_ = new QLineEdit(FromStd(config.youtubeVideo), youtubeBox);
	youtubeVideo_->setPlaceholderText(Text("Settings.YouTube.VideoHint"));
	youtubeMethod_ = new QComboBox(youtubeBox);
	youtubeMethod_->addItem(Text("Settings.YouTube.Method.Stream"), true);
	youtubeMethod_->addItem(Text("Settings.YouTube.Method.Poll"), false);
	youtubeMethod_->setCurrentIndex(config.youtubeStream ? 0 : 1);
	youtubeMethod_->setToolTip(Text("Settings.YouTube.MethodHint"));
	youtubeConnect_ = new QComboBox(youtubeBox);
	youtubeConnect_->addItem(Text("Settings.YouTube.Connect.OnStream"), true);
	youtubeConnect_->addItem(Text("Settings.YouTube.Connect.Always"), false);
	youtubeConnect_->setCurrentIndex(config.youtubeConnectOnStream ? 0 : 1);
	youtubeConnect_->setToolTip(Text("Settings.YouTube.ConnectHint"));
	youtubePoll_ = new QSpinBox(youtubeBox);
	youtubePoll_->setRange(1, 120);
	youtubePoll_->setSuffix(" s");
	youtubePoll_->setValue(config.youtubePollSeconds);
	youtubePoll_->setToolTip(Text("Settings.YouTube.PollHint"));
	youtubeAccount_ = new QLabel(youtubeBox);
	youtubeAccount_->setWordWrap(true);
	youtubeAccount_->setTextInteractionFlags(Qt::TextBrowserInteraction);
	youtubeAccount_->setOpenExternalLinks(true);
	youtubeSignIn_ = MakeButton("Settings.SignIn", youtubeBox);
	youtubeSignOut_ = MakeButton("Settings.SignOut", youtubeBox);
	auto youtubeButtons = new QHBoxLayout();
	youtubeButtons->addWidget(youtubeSignIn_);
	youtubeButtons->addWidget(youtubeSignOut_);
	youtubeButtons->addStretch();
	youtubeForm->addRow(Text("Settings.ClientId"), youtubeClientId_);
	youtubeForm->addRow(Text("Settings.ClientSecret"), youtubeClientSecret_);
	youtubeForm->addRow(Text("Settings.YouTube.Video"), youtubeVideo_);
	youtubeForm->addRow(Text("Settings.YouTube.Connect"), youtubeConnect_);
	youtubeForm->addRow(Text("Settings.YouTube.Method"), youtubeMethod_);
	youtubeForm->addRow(Text("Settings.YouTube.Poll"), youtubePoll_);
	youtubeForm->addRow(Text("Settings.Account"), youtubeAccount_);
	youtubeForm->addRow(QString(), youtubeButtons);
	tabs->addTab(youtubeBox, PlatformIcon(Platform::YouTube), Text("Settings.YouTube"));

	// General
	auto generalBox = new QWidget(tabs);
	auto generalForm = new QFormLayout(generalBox);
	maxMessages_ = new QSpinBox(generalBox);
	maxMessages_->setRange(50, 10000);
	maxMessages_->setSingleStep(50);
	maxMessages_->setValue(config.maxMessages);
	generalForm->addRow(Text("Settings.MaxMessages"), maxMessages_);
	mergeBots_ = new QLineEdit(FromStd(JoinNameList(config.mergeBots)), generalBox);
	mergeBots_->setPlaceholderText(Text("Settings.MergeBotsHint"));
	mergeBots_->setToolTip(Text("Settings.MergeBotsTip"));
	generalForm->addRow(Text("Settings.MergeBots"), mergeBots_);
	tabs->addTab(generalBox, Text("Settings.General"));

	tabs->addTab(MakeLegend(tabs), Text("Legend.Title"));

	auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		if (!twitchChannel_->text().trimmed().isEmpty() &&
		    twitch::NormalizeChannel(twitchChannel_->text().toStdString()).empty()) {
			QMessageBox::warning(this, windowTitle(), Text("Settings.Twitch.InvalidChannel"));
			return;
		}
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(twitchSignIn_, &QPushButton::clicked, this, [this]() { StartLogin(Platform::Twitch); });
	connect(youtubeSignIn_, &QPushButton::clicked, this, [this]() { StartLogin(Platform::YouTube); });
	connect(twitchSignOut_, &QPushButton::clicked, this, [this]() { SignOut(Platform::Twitch); });
	connect(youtubeSignOut_, &QPushButton::clicked, this, [this]() { SignOut(Platform::YouTube); });

	UpdateAccountLabels();
}

SettingsDialog::~SettingsDialog()
{
	login_.reset();
}

ChatConfig SettingsDialog::Result() const
{
	ChatConfig result = config_;
	std::string channel = twitch::NormalizeChannel(twitchChannel_->text().toStdString());
	result.twitchChannel = channel.empty() ? ToStd(twitchChannel_->text()) : channel;
	result.twitchClientId = ToStd(twitchClientId_->text());
	result.youtubeClientId = ToStd(youtubeClientId_->text());
	result.youtubeClientSecret = ToStd(youtubeClientSecret_->text());
	result.youtubeVideo = ToStd(youtubeVideo_->text());
	result.youtubePollSeconds = youtubePoll_->value();
	result.youtubeStream = youtubeMethod_->currentData().toBool();
	result.youtubeConnectOnStream = youtubeConnect_->currentData().toBool();
	result.maxMessages = maxMessages_->value();
	result.mergeBots = SplitNameList(mergeBots_->text().toStdString());
	return result;
}

void SettingsDialog::UpdateAccountLabels()
{
	const bool busy = login_ != nullptr;

	if (config_.twitchToken.IsValid())
		twitchAccount_->setText(Text("Settings.SignedInAs").arg(FromStd(config_.twitchLogin).toHtmlEscaped()));
	else if (!busy || loginPlatform_ != Platform::Twitch)
		twitchAccount_->setText(Text("Settings.Twitch.Anonymous"));
	twitchSignIn_->setEnabled(!busy);
	twitchSignOut_->setEnabled(!busy && config_.twitchToken.IsValid());

	if (config_.youtubeToken.IsValid())
		youtubeAccount_->setText(Text("Settings.SignedIn"));
	else if (!busy || loginPlatform_ != Platform::YouTube)
		youtubeAccount_->setText(Text("Settings.NotSignedIn"));
	youtubeSignIn_->setEnabled(!busy);
	youtubeSignOut_->setEnabled(!busy && config_.youtubeToken.IsValid());
}

void SettingsDialog::StartLogin(Platform platform)
{
	oauth::Provider provider;
	if (platform == Platform::Twitch) {
		provider = oauth::TwitchProvider(ToStd(twitchClientId_->text()));
	} else {
		provider = oauth::GoogleProvider(ToStd(youtubeClientId_->text()), ToStd(youtubeClientSecret_->text()));
		if (provider.clientSecret.empty()) {
			QMessageBox::warning(this, windowTitle(), Text("Settings.MissingClientSecret"));
			return;
		}
	}
	if (provider.clientId.empty()) {
		QMessageBox::warning(this, windowTitle(), Text("Settings.MissingClientId"));
		return;
	}

	QLabel *label = platform == Platform::Twitch ? twitchAccount_ : youtubeAccount_;
	label->setText(Text("Settings.Starting"));
	loginPlatform_ = platform;

	DeviceLogin::Callbacks callbacks;
	callbacks.onCode = [this, label](const oauth::DeviceCode &code) {
		QString uri = FromStd(code.verificationUri);
		QString userCode = FromStd(code.userCode);
		QMetaObject::invokeMethod(
			this,
			[label, uri, userCode]() {
				label->setText(Text("Settings.EnterCode")
						       .arg(uri.toHtmlEscaped(), uri.toHtmlEscaped(),
							    userCode.toHtmlEscaped()));
				QApplication::clipboard()->setText(userCode);
				QDesktopServices::openUrl(QUrl(uri));
			},
			Qt::QueuedConnection);
	};
	callbacks.onFinished = [this, platform](const oauth::Token &token, const std::string &login,
						const std::string &error) {
		QString qlogin = FromStd(login);
		QString qerror = FromStd(error);
		QMetaObject::invokeMethod(
			this,
			[this, platform, token, qlogin, qerror]() { FinishLogin(platform, token, qlogin, qerror); },
			Qt::QueuedConnection);
	};

	login_ = std::make_unique<DeviceLogin>(std::move(provider), std::move(callbacks));
	UpdateAccountLabels();
}

void SettingsDialog::FinishLogin(Platform platform, const oauth::Token &token, const QString &login,
				 const QString &error)
{
	login_.reset();
	if (!error.isEmpty()) {
		UpdateAccountLabels();
		(platform == Platform::Twitch ? twitchAccount_ : youtubeAccount_)
			->setText(Text("Settings.LoginFailed").arg(error.toHtmlEscaped()));
		return;
	}

	if (platform == Platform::Twitch) {
		config_.twitchToken = token;
		config_.twitchLogin = login.toStdString();
		if (twitchChannel_->text().trimmed().isEmpty())
			twitchChannel_->setText(login);
	} else {
		config_.youtubeToken = token;
	}
	UpdateAccountLabels();
}

void SettingsDialog::SignOut(Platform platform)
{
	if (platform == Platform::Twitch) {
		config_.twitchToken = {};
		config_.twitchLogin.clear();
	} else {
		config_.youtubeToken = {};
	}
	UpdateAccountLabels();
}

} // namespace unified_chat
