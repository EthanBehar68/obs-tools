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
	std::string overflowMessage; // Follow, Subscriber: past kMaxQueued waiting
	std::string variantMessage;  // TwitchSub: a resub; Member: a milestone

	// An alert with neither source shows nothing, so it isn't queued (it would only hold up the others).
	bool IsSetUp() const { return enabled && (!source.empty() || !textSource.empty()); }
};

struct AlertsConfig {
	AlertSettings follow{true, {}, {}, "{name} just followed!", "+{count} more new followers!"};
	AlertSettings twitchSub{
		true, {}, {}, "{name} just subscribed!", {}, "{name} resubscribed for {months} months!"};
	AlertSettings giftSubs{true, {}, {}, "{name} gifted {count} subs!"};
	AlertSettings cheer{true, {}, {}, "{name} cheered {amount} bits!"};
	AlertSettings raid{true, {}, {}, "{name} is raiding with {viewers} viewers!"};
	AlertSettings subscriber{true, {}, {}, "{name} just subscribed!", "+{count} more new subscribers!"};
	AlertSettings superChat{true, {}, {}, "{name} sent {amount}!\n{message}"};
	AlertSettings member{
		true, {}, {}, "{name} just became a member!", {}, "{name} has been a member for {months} months!"};
	AlertSettings giftMemberships{true, {}, {}, "{name} gifted {count} memberships!"};
	int minBits = 1;        // smaller cheers don't alert
	int minRaidViewers = 1; // nor smaller raids
	int durationSeconds = 5;
	int subscriberCheckSeconds = 120; // YouTube subscribers: 1 quota unit per check, so 30 an hour

	AlertSettings &For(AlertKind kind);
	const AlertSettings &For(AlertKind kind) const { return const_cast<AlertsConfig *>(this)->For(kind); }
};

// The JSON key of a kind's settings: "follow", "twitch_sub", ...
const char *KindKey(AlertKind kind);

constexpr int kGapMs = 1000; // between two alerts
constexpr int kMinSubscriberCheckSeconds = 30;
constexpr int kMaxSubscriberCheckSeconds = 900;

std::string SerializeAlertsConfig(const AlertsConfig &config);
// Missing or malformed fields fall back to the defaults.
AlertsConfig ParseAlertsConfig(const std::string &json);

} // namespace stream_alerts
