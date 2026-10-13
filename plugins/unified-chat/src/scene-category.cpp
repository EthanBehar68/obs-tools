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

#include "scene-category.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QTimer>

namespace unified_chat {

static QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// The live (program) scene's uuid and name; Studio Mode's preview doesn't count.
static std::string ProgramScene(QString *name = nullptr)
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (!scene)
		return {};
	const char *uuid = obs_source_get_uuid(scene);
	if (name)
		*name = QString::fromUtf8(obs_source_get_name(scene));
	std::string result = uuid ? uuid : "";
	obs_source_release(scene);
	return result;
}

SceneCategorySwitcher::SceneCategorySwitcher(Notice notice, QObject *parent)
	: QObject(parent),
	  notice_(std::move(notice))
{
	timer_ = new QTimer(this);
	timer_->setSingleShot(true);
	connect(timer_, &QTimer::timeout, this, &SceneCategorySwitcher::Fire);
}

SceneCategorySwitcher::~SceneCategorySwitcher()
{
	Stop();
}

void SceneCategorySwitcher::SetConfig(bool enabled, int delaySeconds, std::vector<SceneCategory> links)
{
	enabled_ = enabled;
	delayMs_ = delaySeconds * 1000;
	links_ = std::move(links);
	if (!enabled_) {
		timer_->stop();
		pendingScene_.clear();
	}
}

void SceneCategorySwitcher::OnStreamingChanged(bool streaming)
{
	streaming_ = streaming;
	if (streaming)
		OnSceneChanged(); // the stream may start on a linked scene
	else
		timer_->stop();
}

void SceneCategorySwitcher::OnSceneChanged()
{
	timer_->stop();
	pendingScene_.clear();
	if (!enabled_ || !streaming_)
		return;
	const std::string scene = ProgramScene();
	if (!FindSceneCategory(links_, scene))
		return; // not linked: the category stays as it is
	pendingScene_ = scene;
	timer_->start(delayMs_);
}

void SceneCategorySwitcher::Fire()
{
	QString name;
	const SceneCategory *link = FindSceneCategory(links_, pendingScene_);
	if (!enabled_ || !streaming_ || !link || ProgramScene(&name) != pendingScene_)
		return;
	target_ = *link;
	targetSceneName_ = name;

	// A request still running for an earlier switch is dropped: this scene is what's live now.
	client_.reset();
	const int generation = ++generation_;
	StreamInfoClient::Callbacks callbacks;
	callbacks.onTwitch = [this, generation](StreamInfoClient::TwitchState state) {
		QMetaObject::invokeMethod(
			this,
			[this, generation, state = std::move(state)]() {
				if (generation == generation_)
					OnTwitch(state);
			},
			Qt::QueuedConnection);
	};
	callbacks.onApplied = [this, generation](Platform, std::string error) {
		QMetaObject::invokeMethod(
			this,
			[this, generation, error = std::move(error)]() {
				if (generation != generation_)
					return;
				const QString category = QString::fromStdString(target_.category.name);
				if (error.empty()) {
					obs_log(LOG_INFO, "Twitch category set to '%s' for the scene",
						target_.category.name.c_str());
					notice_(Text("Notice.SceneCategorySet").arg(category, targetSceneName_));
				} else {
					notice_(Text("Notice.SceneCategoryFailed")
							.arg(category, QString::fromStdString(error)));
				}
				Finish();
			},
			Qt::QueuedConnection);
	};
	client_ = std::make_unique<StreamInfoClient>(std::move(callbacks));
	client_->Load(true, false); // the token, your channel and its category now
}

void SceneCategorySwitcher::OnTwitch(const StreamInfoClient::TwitchState &state)
{
	using Status = StreamInfoClient::TwitchState::Status;
	switch (state.status) {
	case Status::Ready:
		break;
	case Status::NeedsScope:
		notice_(Text("Notice.SceneCategoryScope"));
		Finish();
		return;
	case Status::SignedOut:
		notice_(Text("Notice.SceneCategorySignedOut"));
		Finish();
		return;
	case Status::Error:
		notice_(Text("Notice.SceneCategoryFailed")
				.arg(QString::fromStdString(target_.category.name),
				     QString::fromStdString(state.error)));
		Finish();
		return;
	}
	if (state.info.category.id == target_.category.id) {
		Finish(); // already set: nothing to send
		return;
	}
	StreamInfoClient::Update update;
	TwitchChannelUpdate change;
	change.categoryId = target_.category.id;
	update.twitch = change;
	client_->Apply(std::move(update));
}

void SceneCategorySwitcher::Finish()
{
	++generation_;
	client_.reset(); // its thread is idle by now: a quick join
}

void SceneCategorySwitcher::Stop()
{
	timer_->stop();
	pendingScene_.clear();
	Finish();
}

} // namespace unified_chat
