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

#include "alert-queue.hpp"

#include <string>

namespace stream_alerts {

struct AlertSettings {
	bool enabled = true;
	std::string source;     // shown for the alert, then hidden again (e.g. a group or scene with media + text)
	std::string textSource; // gets the message; optional
	std::string message;
	std::string overflowMessage;
};

struct AlertsConfig {
	AlertSettings follow{true, {}, {}, "{name} just followed!", "+{count} more new followers!"};
	AlertSettings subscriber{true, {}, {}, "{name} just subscribed!", "+{count} more new subscribers!"};
	int durationSeconds = 5;

	AlertSettings &For(AlertKind kind) { return kind == AlertKind::Follow ? follow : subscriber; }
	const AlertSettings &For(AlertKind kind) const { return kind == AlertKind::Follow ? follow : subscriber; }
};

constexpr int kGapMs = 1000;                 // between two alerts
constexpr int kSubscriberCheckSeconds = 120; // 30 YouTube quota units an hour, only while streaming

std::string SerializeAlertsConfig(const AlertsConfig &config);
// Missing or malformed fields fall back to the defaults.
AlertsConfig ParseAlertsConfig(const std::string &json);

} // namespace stream_alerts
