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
#include "core/name-color.hpp"
#include "core/text-util.hpp"
#include "core/twitch-irc.hpp"
#include "net/account-store.hpp"
#include "ui/accounts-dialog.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
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
	addRow(html(grey(QStringLiteral("\U0001F507"))), "Legend.NoAudio");
	addRow(html(grey(QStringLiteral("<s>\U0001F441</s>"))), "Legend.NoVideo");
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
	addRow(html(QStringLiteral("\U0001F48E")), "Legend.Event");
	addRow(html(QStringLiteral("\u2728")), "Legend.FirstChatter");

	addRow(html(grey(QStringLiteral("right-click"))), "Legend.Moderate");
	addRow(html(struck(kTagGreyColor, QStringLiteral("(deleted)"))), "Legend.Deleted");
	addRow(html(struck(kTagTimeoutColor, QStringLiteral("(timed out …)"))), "Legend.TimedOut");
	addRow(html(struck(kTagTimeoutColor, QStringLiteral("(removed)"))), "Legend.Removed");
	addRow(html(struck(kTagBanColor, QStringLiteral("(banned)"))), "Legend.Banned");
	addRow(html(struck(kTagGreyColor, QStringLiteral("(chat cleared)"))), "Legend.Cleared");
	addRow(html(QStringLiteral("<i>") + grey(QStringLiteral("notice")) + "</i>"), "Legend.Notice");
	return box;
}

SettingsDialog::SettingsDialog(const ChatConfig &config, const std::string &chatBackground, QWidget *parent)
	: QDialog(parent),
	  config_(config),
	  highlights_(config.highlights),
	  chatBackground_(chatBackground)
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
	twitchMethod_ = new QComboBox(twitchBox);
	twitchMethod_->addItem(Text("Settings.Twitch.Method.EventSub"), true);
	twitchMethod_->addItem(Text("Settings.Twitch.Method.Irc"), false);
	twitchMethod_->setCurrentIndex(config.twitchEventSub ? 0 : 1);
	twitchMethod_->setToolTip(Text("Settings.Twitch.MethodHint"));
	twitchAccount_ = new QLabel(twitchBox);
	twitchAccount_->setWordWrap(true);
	twitchForm->addRow(Text("Settings.Twitch.Channel"), twitchChannel_);
	twitchForm->addRow(Text("Settings.Twitch.Method"), twitchMethod_);
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
	viewerCheck_ = new QSpinBox(generalBox);
	viewerCheck_->setRange(0, 60);
	viewerCheck_->setSuffix(Text("Settings.MinutesSuffix"));
	viewerCheck_->setSpecialValueText(Text("Settings.Off")); // 0
	viewerCheck_->setValue(config.viewerCheckMinutes);
	viewerCheck_->setToolTip(Text("Settings.ViewerCheckHint"));
	generalForm->addRow(Text("Settings.ViewerCheck"), viewerCheck_);
	mergeBots_ = new QLineEdit(FromStd(JoinNameList(config.mergeBots)), generalBox);
	mergeBots_->setPlaceholderText(Text("Settings.MergeBotsHint"));
	mergeBots_->setToolTip(Text("Settings.MergeBotsTip"));
	generalForm->addRow(Text("Settings.MergeBots"), mergeBots_);
	showAlerts_ = new QCheckBox(Text("Settings.ShowAlerts"), generalBox);
	showAlerts_->setChecked(config.showAlerts);
	showAlerts_->setToolTip(Text("Settings.ShowAlertsTip"));
	generalForm->addRow(QString(), showAlerts_);
	tabs->addTab(generalBox, Text("Settings.General"));

	tabs->addTab(MakeHighlightsTab(tabs), Text("Settings.Highlights"));
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
	result.twitchEventSub = twitchMethod_->currentData().toBool();
	result.youtubeVideo = ToStd(youtubeVideo_->text());
	result.viewerCheckMinutes = viewerCheck_->value();
	result.highlights = highlights_;
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

namespace unified_chat {

// A swatch for a colour button; empty colour: the theme's own tint (only the mention highlight allows it).
static QIcon Swatch(const std::string &color, const QColor &theme)
{
	QPixmap pixmap(28, 16);
	pixmap.fill(color.empty() ? theme : QColor(QString::fromStdString(color)));
	return QIcon(pixmap);
}

QWidget *SettingsDialog::MakeHighlightsTab(QWidget *parent)
{
	auto box = new QWidget(parent);
	auto layout = new QVBoxLayout(box);
	auto intro = new QLabel(Text("Settings.Highlights.Intro"), box);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto grid = new QGridLayout();
	grid->setHorizontalSpacing(8);
	grid->addWidget(new QLabel(Text("Settings.Highlights.Show"), box), 0, 1);
	grid->addWidget(new QLabel(Text("Settings.Highlights.Colour"), box), 0, 2);
	grid->addWidget(new QLabel(Text("Settings.Highlights.Emoji"), box), 0, 3);
	for (size_t i = 0; i < kHighlightKinds; ++i) {
		const auto kind = (HighlightKind)i;
		const int row = (int)i + 1;
		HighlightRow &controls = highlightRows_[i];
		grid->addWidget(new QLabel(Text((std::string("Highlight.") + HighlightKey(kind)).c_str()), box), row,
				0);

		controls.show = new QCheckBox(box);
		connect(controls.show, &QCheckBox::toggled, this, [this, i](bool on) {
			highlights_.kinds[i].show = on;
			UpdatePreview();
		});
		grid->addWidget(controls.show, row, 1);

		controls.color = new QToolButton(box);
		controls.color->setIconSize(QSize(28, 16));
		connect(controls.color, &QToolButton::clicked, this, [this, i]() {
			const std::string &current = highlights_.kinds[i].color;
			QColor picked = QColorDialog::getColor(
				current.empty() ? QColor("#808080") : QColor(QString::fromStdString(current)), this);
			if (!picked.isValid())
				return;
			highlights_.kinds[i].color = picked.name().toStdString();
			ShowHighlight(i);
			UpdatePreview();
		});
		grid->addWidget(controls.color, row, 2);

		controls.icon = new QLineEdit(box);
		controls.icon->setMaxLength((int)kMaxIconBytes);
		controls.icon->setFixedWidth(controls.icon->fontMetrics().horizontalAdvance(QStringLiteral("MMMMM")) +
					     16);
		controls.icon->setToolTip(Text("Settings.Highlights.EmojiHint"));
		controls.icon->setEnabled(kind != HighlightKind::Mention); // a mention is a chat line: no emoji
		connect(controls.icon, &QLineEdit::textEdited, this, [this, i](const QString &text) {
			highlights_.kinds[i].icon = SanitizeIcon(text.toStdString());
			UpdatePreview();
		});
		grid->addWidget(controls.icon, row, 3);

		auto reset = new QPushButton(Text("Settings.Highlights.Reset"), box);
		connect(reset, &QPushButton::clicked, this, [this, i]() {
			highlights_.kinds[i] = DefaultHighlight((HighlightKind)i);
			ShowHighlight(i);
			UpdatePreview();
		});
		grid->addWidget(reset, row, 4);
		ShowHighlight(i);
	}
	grid->setColumnStretch(5, 1);
	layout->addLayout(grid);

	// What each highlight looks like on the dock's own background.
	preview_ = new QTextBrowser(box);
	preview_->setOpenLinks(false);
	preview_->setFixedHeight(preview_->fontMetrics().lineSpacing() * 8); // scrolls: keeps the dialog short
	if (!chatBackground_.empty()) {
		QPalette palette = preview_->palette();
		palette.setColor(QPalette::Base, QColor(QString::fromStdString(chatBackground_)));
		preview_->setPalette(palette);
	}
	const qreal dpr = devicePixelRatioF();
	const int size = 16;
	QTextDocument *doc = preview_->document();
	doc->addResource(QTextDocument::ImageResource, QUrl(QString::fromUtf8(kTwitchIconResource)),
			 PlatformImage(Platform::Twitch, size, dpr));
	doc->addResource(QTextDocument::ImageResource, QUrl(QString::fromUtf8(kYouTubeIconResource)),
			 PlatformImage(Platform::YouTube, size, dpr));
	layout->addWidget(new QLabel(Text("Settings.Highlights.Preview"), box));
	layout->addWidget(preview_);
	UpdatePreview();
	return box;
}

void SettingsDialog::ShowHighlight(size_t i)
{
	const Highlight &highlight = highlights_.kinds[i];
	HighlightRow &controls = highlightRows_[i];
	const QSignalBlocker blockShow(controls.show);
	controls.show->setChecked(highlight.show);
	const QColor base = chatBackground_.empty() ? palette().color(QPalette::Base)
						    : QColor(QString::fromStdString(chatBackground_));
	controls.color->setIcon(Swatch(highlight.color, base.lightness() < 128 ? base.lighter(140) : base.darker(112)));
	controls.color->setToolTip(highlight.color.empty() ? Text("Settings.Highlights.Theme")
							   : QString::fromStdString(highlight.color));
	if (controls.icon->text().toStdString() != highlight.icon)
		controls.icon->setText(QString::fromStdString(highlight.icon));
}

void SettingsDialog::UpdatePreview()
{
	if (!preview_)
		return;
	const std::string background = chatBackground_.empty() ? palette().color(QPalette::Base).name().toStdString()
							       : chatBackground_;
	const QColor base(QString::fromStdString(background));
	const QColor themeTint = base.lightness() < 128 ? base.lighter(140) : base.darker(112);
	NameColorResolver names;
	names.SetBackground(background);

	struct Sample {
		HighlightKind kind;
		ChatEvent event;
	};
	const Sample samples[] = {
		{HighlightKind::Follow, {Platform::Twitch, HighlightKind::Follow, "Cool_User followed", {}}},
		{HighlightKind::TwitchSub,
		 {Platform::Twitch, HighlightKind::TwitchSub,
		  "Fan subscribed at Tier 1. They've subscribed for 10 months!", "love this stream"}},
		{HighlightKind::GiftSubs,
		 {Platform::Twitch, HighlightKind::GiftSubs, "Giver is gifting 5 Tier 1 Subs to the community!", {}}},
		{HighlightKind::Cheer, {Platform::Twitch, HighlightKind::Cheer, "Fan cheered 500 bits", "hype!"}},
		{HighlightKind::Raid,
		 {Platform::Twitch, HighlightKind::Raid, "25 raiders from PalStreams have joined!", {}}},
		{HighlightKind::Subscriber, {Platform::YouTube, HighlightKind::Subscriber, "Jane subscribed", {}}},
		{HighlightKind::SuperChat,
		 {Platform::YouTube, HighlightKind::SuperChat, "@viewer sent $5.00", "greetings from Brazil"}},
		{HighlightKind::Member,
		 {Platform::YouTube, HighlightKind::Member, "@member became a member (Gold)", {}}},
		{HighlightKind::GiftMemberships,
		 {Platform::YouTube, HighlightKind::GiftMemberships, "@giver gifted 5 memberships", {}}},
	};

	QTextDocument *doc = preview_->document();
	doc->clear();
	QTextCursor cursor(doc);
	bool first = true;
	auto add = [&](const std::string &html, const QColor &tint) {
		if (!first)
			cursor.insertBlock();
		first = false;
		QTextBlockFormat format;
		if (tint.isValid())
			format.setBackground(tint);
		cursor.setBlockFormat(format);
		cursor.insertHtml(QString::fromStdString(html));
	};
	auto styleOf = [&](HighlightKind kind) {
		return ComputeHighlightStyle(highlights_[kind].color, background);
	};
	for (const auto &sample : samples) {
		if (!highlights_[sample.kind].show)
			continue;
		const HighlightStyle style = styleOf(sample.kind);
		add(FormatEventHtml(sample.event, highlights_[sample.kind].icon, style.text, 16),
		    QColor(QString::fromStdString(style.background)));
	}
	ChatMessage chatter;
	chatter.platform = Platform::Twitch;
	chatter.author = "FirstTimer";
	chatter.text = "hi, just found your stream!";
	if (highlights_[HighlightKind::FirstChatter].show)
		add(FormatMessageHtml(chatter, 16, {Platform::Twitch}, &names, nullptr, 0, nullptr,
				      highlights_[HighlightKind::FirstChatter].icon),
		    QColor(QString::fromStdString(styleOf(HighlightKind::FirstChatter).background)));
	ChatMessage mention = chatter;
	mention.author = "Viewer";
	mention.text = "@you what game is this?";
	if (highlights_[HighlightKind::Mention].show)
		add(FormatMessageHtml(mention, 16, {Platform::Twitch}, &names),
		    highlights_[HighlightKind::Mention].color.empty()
			    ? themeTint
			    : QColor(QString::fromStdString(styleOf(HighlightKind::Mention).background)));
}

} // namespace unified_chat
