/*
obs-stream-alerts
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

#include "alerts-controller.hpp"
#include "alerts-dialog.hpp"
#include "ui/obs-tools-menu.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QTimer>

#include <vector>

namespace stream_alerts {

static QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static const char *KindName(AlertKind kind)
{
	return kind == AlertKind::Follow ? "Twitch follows" : "YouTube subscribers";
}

// Writes the message into a text source (Text (GDI+) and FreeType both use "text").
static void SetSourceText(const std::string &name, const std::string &text)
{
	obs_source_t *source = obs_get_source_by_name(name.c_str());
	if (!source) {
		obs_log(LOG_WARNING, "alert text source '%s' not found", name.c_str());
		return;
	}
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.c_str());
	obs_source_update(source, settings);
	obs_data_release(settings);
	obs_source_release(source);
}

struct ItemMatch {
	obs_source_t *target;
	std::vector<obs_sceneitem_t *> items;
};

static bool CollectItems(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto match = static_cast<ItemMatch *>(param);
	if (obs_sceneitem_get_source(item) == match->target) {
		obs_sceneitem_addref(item);
		match->items.push_back(item);
	}
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, CollectItems, param);
	return true;
}

// Shows or hides the source wherever it's placed: every scene, and groups inside them. Collected first and changed
// after, outside the scenes' enumeration locks.
static void SetSourceVisible(const std::string &name, bool visible)
{
	obs_source_t *target = obs_get_source_by_name(name.c_str());
	if (!target) {
		obs_log(LOG_WARNING, "alert source '%s' not found", name.c_str());
		return;
	}
	ItemMatch match{target, {}};
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i)
		obs_scene_enum_items(obs_scene_from_source(scenes.sources.array[i]), CollectItems, &match);
	obs_frontend_source_list_free(&scenes);
	if (match.items.empty())
		obs_log(LOG_WARNING, "alert source '%s' isn't in any scene", name.c_str());
	for (obs_sceneitem_t *item : match.items) {
		obs_sceneitem_set_visible(item, visible);
		obs_sceneitem_release(item);
	}
	obs_source_release(target);
}

AlertsController::AlertsController(QObject *parent) : QObject(parent)
{
	timer_ = new QTimer(this);
	timer_->setSingleShot(true);
	connect(timer_, &QTimer::timeout, this, [this]() { OnTimer(); });
	LoadConfig();
	followStatus_ = subscriberStatus_ = Text("Status.Off");
}

AlertsController::~AlertsController()
{
	StopListeners();
}

void AlertsController::Start()
{
	if (started_)
		return;
	started_ = true;
	if (obs_frontend_streaming_active()) // the plugin may load while already live
		OnStreamingChanged(true);
}

void AlertsController::Shutdown()
{
	StopListeners();
	timer_->stop();
	started_ = false;
}

void AlertsController::OnStreamingChanged(bool streaming)
{
	if (!started_ || streaming == streaming_)
		return;
	streaming_ = streaming;
	if (streaming) {
		queue_.Reset(); // a new stream: everyone may alert again
		StartListeners(true, true);
	} else {
		StopListeners(); // alerts already waiting still play
	}
}

void AlertsController::OnAccountsChanged(bool twitch, bool google)
{
	if (streaming_)
		StartListeners(twitch, google);
}

void AlertsController::LoadConfig()
{
	char *path = obs_module_config_path("config.json");
	char *text = path ? os_quick_read_utf8_file(path) : nullptr;
	config_ = ParseAlertsConfig(text ? text : "");
	bfree(text);
	bfree(path);
}

void AlertsController::SaveConfig()
{
	char *dir = obs_module_config_path("");
	char *path = obs_module_config_path("config.json");
	if (dir && path) {
		os_mkdirs(dir);
		std::string text = SerializeAlertsConfig(config_);
		if (!os_quick_write_utf8_file_safe(path, text.c_str(), text.size(), false, "tmp", "bak"))
			obs_log(LOG_WARNING, "failed to save %s", path);
	}
	bfree(path);
	bfree(dir);
}

void AlertsController::ApplyConfig(const AlertsConfig &config, bool save)
{
	const bool followChanged = config.follow.enabled != config_.follow.enabled;
	const bool subscriberChanged = config.subscriber.enabled != config_.subscriber.enabled;
	config_ = config;
	if (save)
		SaveConfig();
	if (streaming_ && (followChanged || subscriberChanged))
		StartListeners(followChanged, subscriberChanged);
}

ListenerCallbacks AlertsController::MakeCallbacks(AlertKind kind)
{
	ListenerCallbacks callbacks;
	callbacks.onEvent = [this](AlertEvent event) {
		QMetaObject::invokeMethod(
			this, [this, event = std::move(event)]() { Push(event); }, Qt::QueuedConnection);
	};
	callbacks.onStatus = [this, kind](const std::string &status) {
		obs_log(LOG_INFO, "%s: %s", KindName(kind), status.c_str());
		QString text = QString::fromStdString(status);
		QMetaObject::invokeMethod(
			this,
			[this, kind, text]() {
				(kind == AlertKind::Follow ? followStatus_ : subscriberStatus_) = text;
			},
			Qt::QueuedConnection);
	};
	return callbacks;
}

void AlertsController::StartListeners(bool twitch, bool google)
{
	if (twitch) {
		follows_.reset();
		if (config_.follow.enabled)
			follows_ = std::make_unique<FollowListener>(MakeCallbacks(AlertKind::Follow));
		else
			followStatus_ = Text("Status.Disabled");
	}
	if (google) {
		subscribers_.reset();
		if (config_.subscriber.enabled)
			subscribers_ = std::make_unique<SubscriberPoller>(MakeCallbacks(AlertKind::Subscriber));
		else
			subscriberStatus_ = Text("Status.Disabled");
	}
}

void AlertsController::StopListeners()
{
	follows_.reset();
	subscribers_.reset();
	followStatus_ = subscriberStatus_ = Text("Status.Off");
}

QString AlertsController::StatusText(AlertKind kind) const
{
	return kind == AlertKind::Follow ? followStatus_ : subscriberStatus_;
}

void AlertsController::Test(AlertKind kind)
{
	const char *name = kind == AlertKind::Follow ? "TestFollower" : "TestSubscriber";
	Push({kind, "test-" + std::to_string(++testCount_), name}, true);
}

void AlertsController::Push(const AlertEvent &event, bool test)
{
	if (!queue_.Push(event))
		return;
	obs_log(LOG_INFO, "%s: %s", KindName(event.kind), event.name.c_str());
	// The chat dock shows everyone as they arrive, including those a combined alert will cover.
	unified_chat::NotifyAlert(event.kind == AlertKind::Follow, event.name, test);
	if (phase_ == Phase::Idle)
		ShowNext();
}

void AlertsController::ShowNext()
{
	auto alert = queue_.Pop();
	if (!alert) {
		phase_ = Phase::Idle;
		return;
	}
	const AlertSettings &settings = config_.For(alert->kind);
	if (!settings.textSource.empty())
		SetSourceText(settings.textSource, FormatAlert(*alert, settings.message, settings.overflowMessage));
	shownSource_ = settings.source;
	if (!shownSource_.empty())
		SetSourceVisible(shownSource_, true);
	phase_ = Phase::Showing;
	timer_->start(config_.durationSeconds * 1000);
}

void AlertsController::OnTimer()
{
	if (phase_ == Phase::Showing) {
		if (!shownSource_.empty())
			SetSourceVisible(shownSource_, false);
		phase_ = Phase::Gap;
		timer_->start(kGapMs);
	} else {
		ShowNext();
	}
}

void AlertsController::OpenSettings(QWidget *parent)
{
	AlertsDialog dialog(this, parent);
	dialog.exec();
}

} // namespace stream_alerts
