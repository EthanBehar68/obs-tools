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

#pragma once

#include "core/alert-queue.hpp"
#include "core/alerts-config.hpp"
#include "net/listeners.hpp"

#include <QObject>
#include <QString>

#include <memory>

class QTimer;
class QWidget;

namespace stream_alerts {

// Runs on the UI thread. Listens for Twitch and YouTube events while OBS streams, and plays alerts one at a time by
// writing the message into the text source and showing the alert source for the configured time.
class AlertsController : public QObject {
public:
	explicit AlertsController(QObject *parent = nullptr);
	~AlertsController() override;

	void Start(); // OBS finished loading
	void Shutdown();
	void OnStreamingChanged(bool streaming);
	void OnAccountsChanged(bool twitch, bool google);
	// A Super Chat or membership from Unified Chat's YouTube chat (core/youtube-events JSON).
	void OnYouTubeEvent(const std::string &eventJson);
	void OpenSettings(QWidget *parent);

	const AlertsConfig &Config() const { return config_; }
	// save = false: the settings window trying values out (Test), undone on Cancel.
	void ApplyConfig(const AlertsConfig &config, bool save);
	// variant: a resub / member milestone instead of a new sub / member.
	void Test(AlertKind kind, bool variant = false);
	QString StatusText(AlertKind kind) const;

private:
	enum class Phase { Idle, Showing, Gap };

	void LoadConfig();
	void SaveConfig();
	void StartListeners(bool twitch, bool google);
	void StopListeners();
	ListenerCallbacks MakeCallbacks(bool twitch);
	TwitchWants Wants() const;
	void Push(const AlertEvent &event, bool test = false);
	void ShowNext();
	void OnTimer();

	AlertsConfig config_;
	AlertQueue queue_;
	std::unique_ptr<TwitchListener> twitch_;
	std::unique_ptr<SubscriberPoller> subscribers_;
	QString twitchStatus_;
	QString subscriberStatus_;
	QTimer *timer_;
	Phase phase_ = Phase::Idle;
	std::string shownSource_;
	bool started_ = false;
	bool streaming_ = false;
	int testCount_ = 0;
};

} // namespace stream_alerts
