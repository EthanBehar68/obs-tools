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

#include "stream-info-dialog.hpp"
#include "core/text-util.hpp"
#include "ui/accounts-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QToolButton>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace unified_chat {

static constexpr int kBoxArtWidth = 26; // shown; fetched at twice the size for high-DPI screens
static constexpr int kBoxArtHeight = 36;
static constexpr int kSearchDelayMs = 300; // a search once typing pauses, not one per key
static constexpr int kIdRole = Qt::UserRole;
static constexpr int kNameRole = Qt::UserRole + 1;
static constexpr int kBoxArtRole = Qt::UserRole + 2; // the URL Twitch gave
static constexpr int kRecentRole = Qt::UserRole + 3; // index into recent_, or -1

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

StreamInfoDialog::StreamInfoDialog(const ChatConfig &config, SaveSettings save, QWidget *parent)
	: QDialog(parent),
	  save_(std::move(save)),
	  recent_(config.recentCategories),
	  sceneLinks_(config.sceneCategories)
{
	setWindowTitle(Text("StreamInfo.Title"));
	setMinimumWidth(520);

	auto layout = new QVBoxLayout(this);
	twitchStatus_ = new QLabel(this);
	youtubeStatus_ = new QLabel(this);
	for (QLabel *label : {twitchStatus_, youtubeStatus_}) {
		label->setWordWrap(true);
		layout->addWidget(label);
	}

	auto form = new QFormLayout();
	title_ = new QLineEdit(this);
	titleCount_ = new QLabel(this);
	auto titleRow = new QHBoxLayout();
	titleRow->addWidget(title_, 1);
	titleRow->addWidget(titleCount_);
	form->addRow(Text("StreamInfo.StreamTitle"), titleRow);

	// Twitch category: the selected one, a search box, and the matches (or recent ones) below it.
	categoryIcon_ = new QLabel(this);
	categoryIcon_->setFixedSize(kBoxArtWidth, kBoxArtHeight);
	category_ = new QLabel(this);
	auto categoryRow = new QHBoxLayout();
	categoryRow->addWidget(categoryIcon_);
	categoryRow->addWidget(category_, 1);
	form->addRow(Text("StreamInfo.Category"), categoryRow);
	search_ = new QLineEdit(this);
	search_->setPlaceholderText(Text("StreamInfo.SearchHint"));
	search_->setClearButtonEnabled(true);
	form->addRow(QString(), search_);
	categories_ = new QListWidget(this);
	categories_->setIconSize(QSize(kBoxArtWidth, kBoxArtHeight));
	// Lists grow with the window, so a taller window shows more rather than spreading the rows apart.
	categories_->setMinimumHeight((kBoxArtHeight + 6) * 3);
	form->addRow(QString(), categories_);

	// Link the selected category to a scene: switching to it while streaming sets the category.
	useForScene_ = new QToolButton(this);
	useForScene_->setText(Text("StreamInfo.UseForScene"));
	useForScene_->setToolTip(Text("StreamInfo.UseForSceneHint"));
	useForScene_->setPopupMode(QToolButton::InstantPopup);
	sceneMenu_ = new QMenu(useForScene_);
	useForScene_->setMenu(sceneMenu_);
	connect(sceneMenu_, &QMenu::aboutToShow, this, &StreamInfoDialog::FillSceneMenu);
	form->addRow(QString(), useForScene_);

	tags_ = new QLineEdit(this);
	tags_->setPlaceholderText(Text("StreamInfo.TagsHint"));
	form->addRow(Text("StreamInfo.Tags"), tags_);
	tagsWarning_ = new QLabel(this);
	tagsWarning_->setWordWrap(true);
	tagsWarning_->setProperty("class", "text-danger"); // OBS themes' error colour
	tagsWarning_->hide();
	form->addRow(QString(), tagsWarning_);

	youtubeCategory_ = new QComboBox(this);
	for (const auto &category : kYouTubeCategories)
		youtubeCategory_->addItem(QString::fromUtf8(category.name), QString::fromUtf8(category.id));
	form->addRow(Text("StreamInfo.YouTubeCategory"), youtubeCategory_);

	changeTwitch_ = new QCheckBox(Text("StreamInfo.ChangeTwitch"), this);
	changeTwitch_->setChecked(config.streamInfoTwitch);
	changeYouTube_ = new QCheckBox(Text("StreamInfo.ChangeYouTube").arg(kYouTubeUpdateCost + 1), this);
	changeYouTube_->setChecked(config.streamInfoYouTube);
	auto targets = new QHBoxLayout();
	targets->addWidget(changeTwitch_);
	targets->addWidget(changeYouTube_);
	targets->addStretch();
	form->addRow(Text("StreamInfo.Change"), targets);
	layout->addLayout(form, 1);

	// Scenes linked to a category.
	auto scenesTitle = new QLabel(Text("StreamInfo.Scenes"), this);
	QFont bold = scenesTitle->font();
	bold.setBold(true);
	scenesTitle->setFont(bold);
	layout->addWidget(scenesTitle);
	byScene_ = new QCheckBox(Text("StreamInfo.ByScene"), this);
	byScene_->setChecked(config.categoryByScene);
	sceneDelay_ = new QSpinBox(this);
	sceneDelay_->setRange(0, kMaxSceneCategoryDelay);
	sceneDelay_->setSuffix(" s");
	sceneDelay_->setValue(config.categorySceneDelaySeconds);
	sceneDelay_->setToolTip(Text("StreamInfo.SceneDelayHint"));
	auto byRow = new QHBoxLayout();
	byRow->addWidget(byScene_);
	byRow->addWidget(sceneDelay_);
	byRow->addStretch();
	layout->addLayout(byRow);
	sceneList_ = new QListWidget(this);
	sceneList_->setIconSize(QSize(kBoxArtWidth, kBoxArtHeight));
	sceneList_->setMinimumHeight((kBoxArtHeight + 6) * 2);
	unlink_ = new QPushButton(Text("StreamInfo.Unlink"), this);
	auto listRow = new QHBoxLayout();
	listRow->addWidget(sceneList_, 1);
	listRow->addWidget(unlink_, 0, Qt::AlignTop);
	layout->addLayout(listRow, 1);
	connect(byScene_, &QCheckBox::toggled, this, [this]() { Save(); });
	connect(sceneDelay_, &QSpinBox::valueChanged, this, [this]() { Save(); });
	connect(sceneList_, &QListWidget::currentRowChanged, this,
		[this](int row) { unlink_->setEnabled(row >= 0 && row < (int)sceneLinks_.size()); });
	connect(unlink_, &QPushButton::clicked, this, [this]() {
		const int row = sceneList_->currentRow();
		if (row < 0 || row >= (int)sceneLinks_.size())
			return;
		UnlinkSceneCategory(sceneLinks_, sceneLinks_[row].sceneUuid);
		ShowSceneLinks();
		Save();
	});

	result_ = new QLabel(this);
	result_->setWordWrap(true);
	layout->addWidget(result_);

	auto buttons = new QHBoxLayout();
	signIn_ = new QPushButton(Text("StreamInfo.SignIn"), this);
	auto reload = new QPushButton(Text("StreamInfo.Reload"), this);
	apply_ = new QPushButton(Text("StreamInfo.Apply"), this);
	auto close = new QPushButton(Text("StreamInfo.Close"), this);
	for (QPushButton *button : {signIn_, reload, apply_, close})
		button->setAutoDefault(false); // Enter in the search box shouldn't press Apply
	buttons->addWidget(signIn_);
	buttons->addStretch();
	buttons->addWidget(reload);
	buttons->addWidget(apply_);
	buttons->addWidget(close);
	layout->addLayout(buttons);

	searchTimer_ = new QTimer(this);
	searchTimer_->setSingleShot(true);
	searchTimer_->setInterval(kSearchDelayMs);

	connect(title_, &QLineEdit::textEdited, this, [this]() {
		titleEdited_ = true;
		UpdateTitleCount();
	});
	connect(search_, &QLineEdit::textChanged, this, [this](const QString &text) {
		if (text.trimmed().isEmpty()) {
			searchTimer_->stop();
			ShowCategories({}, true);
		} else {
			searchTimer_->start();
		}
	});
	connect(searchTimer_, &QTimer::timeout, this, [this]() {
		if (client_)
			client_->Search(ToStd(search_->text()));
	});
	connect(categories_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
		const TwitchCategory category{item->data(kIdRole).toString().toStdString(),
					      item->data(kNameRole).toString().toStdString(),
					      item->data(kBoxArtRole).toString().toStdString()};
		const int recent = item->data(kRecentRole).toInt();
		const bool hasTags = recent >= 0 && recent < (int)recent_.size() && !recent_[recent].tags.empty();
		SelectCategory(category, hasTags ? &recent_[recent].tags : nullptr);
	});
	connect(tags_, &QLineEdit::textChanged, this, [this]() { UpdateControls(); });
	for (QCheckBox *box : {changeTwitch_, changeYouTube_}) {
		connect(box, &QCheckBox::toggled, this, [this]() {
			UpdateTitleCount();
			UpdateControls();
			Save();
		});
	}
	connect(signIn_, &QPushButton::clicked, this, [this]() {
		obs_tools::OpenAccountsDialog(this); // modal
		Reload();
	});
	connect(reload, &QPushButton::clicked, this, &StreamInfoDialog::Reload);
	connect(apply_, &QPushButton::clicked, this, &StreamInfoDialog::Apply);
	connect(close, &QPushButton::clicked, this, &QDialog::close);

	// Callbacks come from the client's thread; queued to this window, they're dropped once it's gone.
	StreamInfoClient::Callbacks callbacks;
	callbacks.onTwitch = [this](StreamInfoClient::TwitchState state) {
		QMetaObject::invokeMethod(
			this, [this, state = std::move(state)]() mutable { OnTwitch(std::move(state)); },
			Qt::QueuedConnection);
	};
	callbacks.onYouTube = [this](StreamInfoClient::YouTubeState state) {
		QMetaObject::invokeMethod(
			this, [this, state = std::move(state)]() mutable { OnYouTube(std::move(state)); },
			Qt::QueuedConnection);
	};
	callbacks.onCategories = [this](std::string query, std::vector<TwitchCategory> categories) {
		QMetaObject::invokeMethod(
			this,
			[this, query = std::move(query), categories = std::move(categories)]() {
				OnCategories(query, categories);
			},
			Qt::QueuedConnection);
	};
	callbacks.onImage = [this](std::string url, std::string bytes) {
		QMetaObject::invokeMethod(
			this, [this, url = std::move(url), bytes = std::move(bytes)]() { OnImage(url, bytes); },
			Qt::QueuedConnection);
	};
	callbacks.onApplied = [this](Platform platform, std::string error) {
		QMetaObject::invokeMethod(
			this, [this, platform, error = std::move(error)]() { OnApplied(platform, error); },
			Qt::QueuedConnection);
	};
	client_ = std::make_unique<StreamInfoClient>(std::move(callbacks));
	ShowSceneLinks(); // box art comes through the client
	Reload();
}

StreamInfoDialog::~StreamInfoDialog()
{
	client_.reset(); // stops and joins the thread before the widgets its callbacks reach go away
}

void StreamInfoDialog::Reload()
{
	twitch_.reset();
	youtube_.reset();
	titleEdited_ = false;
	applying_ = 0;
	selected_ = {};
	title_->clear();
	tags_->clear();
	result_->clear();
	twitchStatus_->setText(Text("StreamInfo.TwitchLoading"));
	youtubeStatus_->setText(Text("StreamInfo.YouTubeLoading"));
	SelectCategory({}, nullptr);
	ShowCategories({}, true);
	UpdateTitleCount();
	UpdateControls();
	client_->Load(true, true);
}

void StreamInfoDialog::OnTwitch(StreamInfoClient::TwitchState state)
{
	using Status = StreamInfoClient::TwitchState::Status;
	const QString login = FromStd(state.login);
	switch (state.status) {
	case Status::SignedOut:
		twitchStatus_->setText(Text("StreamInfo.TwitchSignedOut"));
		break;
	case Status::NeedsScope:
		twitchStatus_->setText(Text("StreamInfo.TwitchNeedsScope").arg(login));
		break;
	case Status::Ready:
		twitchStatus_->setText(Text("StreamInfo.TwitchReady").arg(login));
		break;
	case Status::Error:
		twitchStatus_->setText(Text("StreamInfo.TwitchError").arg(FromStd(state.error)));
		break;
	}
	if (state.status == Status::Ready || state.status == Status::NeedsScope) {
		if (!titleEdited_)
			title_->setText(
				FromStd(state.info.title)); // Twitch's title first; YouTube's fills in without one
		SelectCategory(state.info.category, &state.info.tags);
	}
	twitch_ = std::move(state);
	UpdateTitleCount();
	UpdateControls();
}

void StreamInfoDialog::OnYouTube(StreamInfoClient::YouTubeState state)
{
	using Status = StreamInfoClient::YouTubeState::Status;
	switch (state.status) {
	case Status::SignedOut:
		youtubeStatus_->setText(Text("StreamInfo.YouTubeSignedOut"));
		break;
	case Status::NoBroadcast:
		youtubeStatus_->setText(Text("StreamInfo.YouTubeNoBroadcast"));
		break;
	case Status::Ready:
		youtubeStatus_->setText(
			Text(state.broadcast.live ? "StreamInfo.YouTubeLive" : "StreamInfo.YouTubeUpcoming")
				.arg(FromStd(state.broadcast.title)));
		break;
	case Status::Error:
		youtubeStatus_->setText(Text("StreamInfo.YouTubeError").arg(FromStd(state.error)));
		break;
	}
	if (state.status == Status::Ready) {
		const QString id = FromStd(state.snippet.categoryId);
		int index = youtubeCategory_->findData(id);
		if (index < 0 && !id.isEmpty()) { // one this list doesn't have: kept as it is
			youtubeCategory_->addItem(Text("StreamInfo.OtherCategory").arg(id), id);
			index = youtubeCategory_->count() - 1;
		}
		youtubeCategory_->setCurrentIndex(index);
		if (!titleEdited_ && title_->text().isEmpty())
			title_->setText(FromStd(state.snippet.title));
	}
	youtube_ = std::move(state);
	UpdateTitleCount();
	UpdateControls();
}

void StreamInfoDialog::OnCategories(const std::string &query, const std::vector<TwitchCategory> &categories)
{
	if (query != ToStd(search_->text()))
		return; // typing went on; a newer search is coming
	ShowCategories(categories, false);
}

void StreamInfoDialog::ShowCategories(const std::vector<TwitchCategory> &categories, bool recent)
{
	categories_->clear();
	auto add = [this](const TwitchCategory &category, int recentIndex) {
		auto item = new QListWidgetItem(BoxArt(category), FromStd(category.name), categories_);
		item->setData(kIdRole, FromStd(category.id));
		item->setData(kNameRole, FromStd(category.name));
		item->setData(kBoxArtRole, FromStd(category.boxArtUrl));
		item->setData(kRecentRole, recentIndex);
		if (recentIndex >= 0 && !recent_[recentIndex].tags.empty())
			item->setToolTip(FromStd(JoinTags(recent_[recentIndex].tags)));
	};
	if (recent) {
		for (size_t i = 0; i < recent_.size(); ++i)
			add(recent_[i].category, (int)i);
		return;
	}
	for (const auto &category : categories)
		add(category, -1);
	if (categories.empty())
		categories_->addItem(Text("StreamInfo.NoMatches"));
}

void StreamInfoDialog::SelectCategory(const TwitchCategory &category, const std::vector<std::string> *tags)
{
	selected_ = category;
	category_->setText(category.id.empty() ? Text("StreamInfo.NoCategory") : FromStd(category.name));
	categoryIcon_->setPixmap(BoxArt(category));
	if (tags)
		tags_->setText(FromStd(JoinTags(*tags)));
	UpdateControls();
}

QPixmap StreamInfoDialog::BoxArt(const TwitchCategory &category)
{
	const std::string url = BoxArtUrl(category, kBoxArtWidth * 2, kBoxArtHeight * 2);
	if (url.empty())
		return {};
	const QString key = FromStd(url);
	if (auto it = boxArt_.find(key); it != boxArt_.end())
		return *it;
	if (!requested_.contains(key)) {
		requested_.insert(key, true);
		client_->FetchImage(url);
	}
	return {};
}

void StreamInfoDialog::OnImage(const std::string &url, const std::string &bytes)
{
	QPixmap pixmap;
	if (!pixmap.loadFromData(reinterpret_cast<const uchar *>(bytes.data()), (uint)bytes.size()))
		return;
	pixmap = pixmap.scaled(kBoxArtWidth * 2, kBoxArtHeight * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	pixmap.setDevicePixelRatio(2.0);
	boxArt_.insert(FromStd(url), pixmap);
	// Whatever shows this picture: the selected category and the list rows.
	if (BoxArtUrl(selected_, kBoxArtWidth * 2, kBoxArtHeight * 2) == url)
		categoryIcon_->setPixmap(pixmap);
	for (int row = 0; row < categories_->count(); ++row) {
		QListWidgetItem *item = categories_->item(row);
		const TwitchCategory category{item->data(kIdRole).toString().toStdString(),
					      {},
					      item->data(kBoxArtRole).toString().toStdString()};
		if (!category.id.empty() && BoxArtUrl(category, kBoxArtWidth * 2, kBoxArtHeight * 2) == url)
			item->setIcon(pixmap);
	}
	for (int row = 0; row < sceneList_->count() && row < (int)sceneLinks_.size(); ++row) {
		if (BoxArtUrl(sceneLinks_[row].category, kBoxArtWidth * 2, kBoxArtHeight * 2) == url)
			sceneList_->item(row)->setIcon(pixmap);
	}
}

void StreamInfoDialog::UpdateTitleCount()
{
	const size_t length = Utf8Length(ToStd(title_->text()));
	const bool youtube = changeYouTube_->isChecked();
	const size_t limit = youtube ? kYouTubeTitleMax : kTwitchTitleMax;
	titleCount_->setText(QString("%1 / %2").arg(length).arg(limit));
	titleCount_->setProperty("class", length > limit ? "text-danger" : "");
	titleCount_->style()->unpolish(titleCount_);
	titleCount_->style()->polish(titleCount_);
}

void StreamInfoDialog::UpdateControls()
{
	using TwitchStatus = StreamInfoClient::TwitchState::Status;
	using YouTubeStatus = StreamInfoClient::YouTubeState::Status;
	const bool twitchReady = twitch_ && twitch_->status == TwitchStatus::Ready;
	const bool twitchRead = twitch_ && (twitchReady || twitch_->status == TwitchStatus::NeedsScope);
	const bool youtubeReady = youtube_ && youtube_->status == YouTubeStatus::Ready;
	signIn_->setVisible(
		twitch_ && (twitch_->status == TwitchStatus::SignedOut || twitch_->status == TwitchStatus::NeedsScope));
	search_->setEnabled(twitchRead);
	categories_->setEnabled(twitchRead);
	useForScene_->setEnabled(!selected_.id.empty());
	tags_->setEnabled(twitchRead);
	youtubeCategory_->setEnabled(youtubeReady);
	changeTwitch_->setEnabled(twitchReady);
	changeYouTube_->setEnabled(youtubeReady);

	std::vector<std::string> rejected;
	ParseTags(ToStd(tags_->text()), &rejected);
	QStringList bad;
	for (const auto &word : rejected)
		bad.append(FromStd(word));
	tagsWarning_->setText(bad.isEmpty() ? QString() : Text("StreamInfo.BadTags").arg(bad.join(", ")));
	tagsWarning_->setVisible(!bad.isEmpty());

	const bool twitch = twitchReady && changeTwitch_->isChecked();
	const bool youtube = youtubeReady && changeYouTube_->isChecked();
	apply_->setEnabled(applying_ == 0 && (twitch || youtube));
}

void StreamInfoDialog::Apply()
{
	using TwitchStatus = StreamInfoClient::TwitchState::Status;
	using YouTubeStatus = StreamInfoClient::YouTubeState::Status;
	const bool twitch = twitch_ && twitch_->status == TwitchStatus::Ready && changeTwitch_->isChecked();
	const bool youtube = youtube_ && youtube_->status == YouTubeStatus::Ready && changeYouTube_->isChecked();
	const std::string title = ToStd(title_->text());
	const size_t length = Utf8Length(title);

	QStringList problems;
	if (title.empty())
		problems.append(Text("StreamInfo.NoTitle"));
	if (twitch && length > kTwitchTitleMax)
		problems.append(Text("StreamInfo.TitleTooLong").arg("Twitch").arg(kTwitchTitleMax));
	if (youtube && length > kYouTubeTitleMax)
		problems.append(Text("StreamInfo.TitleTooLong").arg("YouTube").arg(kYouTubeTitleMax));
	if (youtube && !YouTubeTitleAllowed(title))
		problems.append(Text("StreamInfo.YouTubeBrackets"));
	std::vector<std::string> rejected;
	const std::vector<std::string> tags = ParseTags(ToStd(tags_->text()), &rejected);
	if (twitch && !rejected.empty())
		problems.append(tagsWarning_->text());
	if (!problems.isEmpty()) {
		result_->setText(problems.join("\n"));
		return;
	}

	// Only what changed, so a platform keeps whatever this window didn't touch.
	StreamInfoClient::Update update;
	if (twitch) {
		TwitchChannelUpdate change;
		const TwitchChannelInfo &current = twitch_->info;
		if (title != current.title)
			change.title = title;
		if (selected_.id != current.category.id)
			change.categoryId = selected_.id;
		if (tags != current.tags)
			change.tags = tags;
		if (change.title || change.categoryId || change.tags)
			update.twitch = std::move(change);
	}
	if (youtube) {
		const std::string category = youtubeCategory_->currentData().toString().toStdString();
		const YouTubeVideoSnippet &current = youtube_->snippet;
		if (title != current.title)
			update.youtubeTitle = title;
		if (!category.empty() && category != current.categoryId)
			update.youtubeCategoryId = category;
		update.youtube = update.youtubeTitle || update.youtubeCategoryId;
		update.youtubeVideoId = youtube_->broadcast.id;
	}
	if (!update.twitch && !update.youtube) {
		result_->setText(Text("StreamInfo.NothingToChange"));
		return;
	}
	applying_ = (update.twitch ? 1 : 0) + (update.youtube ? 1 : 0);
	result_->setText(Text("StreamInfo.Saving"));
	UpdateControls();
	client_->Apply(std::move(update));
}

void StreamInfoDialog::OnApplied(Platform platform, const std::string &error)
{
	if (applying_ > 0)
		--applying_;
	const bool twitch = platform == Platform::Twitch;
	const QString name = twitch ? "Twitch" : "YouTube";
	const QString line = error.empty() ? Text("StreamInfo.Saved").arg(name)
					   : Text("StreamInfo.NotSaved").arg(name, FromStd(error));
	// One line per platform, in the order they answer.
	const QString previous = result_->text() == Text("StreamInfo.Saving") ? QString() : result_->text();
	result_->setText(previous.isEmpty() ? line : previous + "\n" + line);

	if (error.empty()) {
		// What's set now, so the next Apply only sends what changes after this.
		const std::string title = ToStd(title_->text());
		if (twitch && twitch_) {
			twitch_->info.title = title;
			twitch_->info.category = selected_;
			twitch_->info.tags = ParseTags(ToStd(tags_->text()));
			RememberCategory(recent_, {selected_, twitch_->info.tags});
			Save();
			if (search_->text().trimmed().isEmpty())
				ShowCategories({}, true);
		} else if (!twitch && youtube_) {
			youtube_->snippet.title = title;
			youtube_->snippet.categoryId = youtubeCategory_->currentData().toString().toStdString();
		}
	}
	UpdateControls();
}

void StreamInfoDialog::Save()
{
	if (!save_)
		return;
	ChatConfig settings;
	settings.streamInfoTwitch = changeTwitch_->isChecked();
	settings.streamInfoYouTube = changeYouTube_->isChecked();
	settings.recentCategories = recent_;
	settings.categoryByScene = byScene_->isChecked();
	settings.categorySceneDelaySeconds = sceneDelay_->value();
	settings.sceneCategories = sceneLinks_;
	save_(settings);
}

// The scene's name now, or the one saved with the link when it isn't in this scene collection.
static QString SceneName(const SceneCategory &link, bool *present = nullptr)
{
	obs_source_t *scene = obs_get_source_by_uuid(link.sceneUuid.c_str());
	if (present)
		*present = scene != nullptr;
	const QString name = scene ? QString::fromUtf8(obs_source_get_name(scene)) : FromStd(link.sceneName);
	obs_source_release(scene);
	return name;
}

void StreamInfoDialog::ShowSceneLinks()
{
	sceneList_->clear();
	for (const auto &link : sceneLinks_) {
		bool present = false;
		const QString scene = SceneName(link, &present);
		QString text = Text("StreamInfo.SceneLink").arg(scene, FromStd(link.category.name));
		if (!present)
			text += " " + Text("StreamInfo.OtherCollection");
		sceneList_->addItem(new QListWidgetItem(BoxArt(link.category), text));
	}
	unlink_->setEnabled(false);
}

void StreamInfoDialog::FillSceneMenu()
{
	sceneMenu_->clear();
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_source_t *scene = scenes.sources.array[i];
		const std::string uuid = obs_source_get_uuid(scene);
		const std::string name = obs_source_get_name(scene);
		QString label = QString::fromStdString(name);
		if (const SceneCategory *link = FindSceneCategory(sceneLinks_, uuid))
			label += "  (" + FromStd(link->category.name) + ")";
		connect(sceneMenu_->addAction(label), &QAction::triggered, this, [this, uuid, name]() {
			LinkSceneCategory(sceneLinks_, {uuid, name, selected_});
			ShowSceneLinks();
			Save();
			result_->setText(
				Text("StreamInfo.Linked").arg(QString::fromStdString(name), FromStd(selected_.name)));
		});
	}
	obs_frontend_source_list_free(&scenes);
}

} // namespace unified_chat
