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
#include "net/account-store.hpp"
#include "ui/accounts-dialog.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
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

	addRow(html(grey(QStringLiteral("right-click"))), "Legend.Moderate");
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
	twitchAccount_ = new QLabel(twitchBox);
	twitchAccount_->setWordWrap(true);
	twitchForm->addRow(Text("Settings.Twitch.Channel"), twitchChannel_);
	twitchForm->addRow(Text("Settings.Account"), AccountRow(twitchAccount_, twitchBox));
	tabs->addTab(twitchBox, PlatformIcon(Platform::Twitch), Text("Settings.Twitch"));

	// YouTube
	auto youtubeBox = new QWidget(tabs);
	auto youtubeForm = new QFormLayout(youtubeBox);
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
	youtubeForm->addRow(Text("Settings.YouTube.Video"), youtubeVideo_);
	youtubeForm->addRow(Text("Settings.YouTube.Connect"), youtubeConnect_);
	youtubeForm->addRow(Text("Settings.YouTube.Method"), youtubeMethod_);
	youtubeForm->addRow(Text("Settings.YouTube.Poll"), youtubePoll_);
	youtubeForm->addRow(Text("Settings.Account"), AccountRow(youtubeAccount_, youtubeBox));
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
	showAlerts_ = new QCheckBox(Text("Settings.ShowAlerts"), generalBox);
	showAlerts_->setChecked(config.showAlerts);
	showAlerts_->setToolTip(Text("Settings.ShowAlertsTip"));
	generalForm->addRow(QString(), showAlerts_);
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

	UpdateAccountLabels();
}

ChatConfig SettingsDialog::Result() const
{
	ChatConfig result = config_;
	std::string channel = twitch::NormalizeChannel(twitchChannel_->text().toStdString());
	result.twitchChannel = channel.empty() ? ToStd(twitchChannel_->text()) : channel;
	result.youtubeVideo = ToStd(youtubeVideo_->text());
	result.youtubePollSeconds = youtubePoll_->value();
	result.youtubeStream = youtubeMethod_->currentData().toBool();
	result.youtubeConnectOnStream = youtubeConnect_->currentData().toBool();
	result.maxMessages = maxMessages_->value();
	result.mergeBots = SplitNameList(mergeBots_->text().toStdString());
	result.showAlerts = showAlerts_->isChecked();
	return result;
}

// The account's status, and a button to the shared Accounts window where signing in happens.
QLayout *SettingsDialog::AccountRow(QLabel *label, QWidget *parent)
{
	auto row = new QHBoxLayout();
	auto button = new QPushButton(Text("Settings.Accounts"), parent);
	button->setToolTip(Text("Settings.AccountsTip"));
	connect(button, &QPushButton::clicked, this, &SettingsDialog::OpenAccounts);
	row->addWidget(label, 1);
	row->addWidget(button);
	return row;
}

void SettingsDialog::UpdateAccountLabels()
{
	const Accounts accounts = SharedAccounts().Load();
	if (accounts.twitch.token.IsValid())
		twitchAccount_->setText(
			Text("Settings.SignedInAs").arg(FromStd(accounts.twitch.login).toHtmlEscaped()));
	else
		twitchAccount_->setText(Text("Settings.Twitch.Anonymous"));
	youtubeAccount_->setText(Text(accounts.google.token.IsValid() ? "Settings.SignedIn" : "Settings.NotSignedIn"));
}

void SettingsDialog::OpenAccounts()
{
	OpenAccountsDialog(this);
	UpdateAccountLabels();
	// Signed in without a channel set: your own channel is the likely one.
	if (twitchChannel_->text().trimmed().isEmpty())
		twitchChannel_->setText(FromStd(SharedAccounts().Load().twitch.login));
}

} // namespace unified_chat
