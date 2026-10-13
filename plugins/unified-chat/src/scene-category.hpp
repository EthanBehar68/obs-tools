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

#include "core/obs-tools.hpp"

#include "core/stream-info.hpp"
#include "net/stream-info-client.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QTimer;

namespace unified_chat {

// While streaming, sets the Twitch category of a scene linked to one, once that scene has been live (program) for
// the delay, so a quick hop to BRB and back changes nothing. Scenes without a link leave the category alone. Only
// the category is sent, and only when Twitch has another one. Nothing runs between switches: a request uses a
// short-lived StreamInfoClient. UI thread.
class SceneCategorySwitcher : public QObject {
public:
	using Notice = std::function<void(const QString &)>;

	SceneCategorySwitcher(Notice notice, QObject *parent);
	~SceneCategorySwitcher() override;

	void SetConfig(bool enabled, int delaySeconds, std::vector<SceneCategory> links);
	void OnSceneChanged();
	void OnStreamingChanged(bool streaming);
	void Stop(); // OBS is closing

private:
	void Fire();
	void OnTwitch(const StreamInfoClient::TwitchState &state);
	void Finish();

	Notice notice_;
	bool enabled_ = false;
	int delayMs_ = kDefaultSceneCategoryDelay * 1000;
	std::vector<SceneCategory> links_;
	bool streaming_ = false;
	QTimer *timer_;
	std::string pendingScene_; // uuid of the scene the timer is for
	SceneCategory target_;
	QString targetSceneName_;
	std::unique_ptr<StreamInfoClient> client_;
	int generation_ = 0; // callbacks from a client already replaced are dropped
};

} // namespace unified_chat
