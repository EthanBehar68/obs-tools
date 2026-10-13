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
#include <string_view>
#include <vector>

namespace stream_alerts {

struct AlertSettings {
	bool enabled = true;
	std::string source;     // shown for the alert, then hidden again (e.g. a group or scene with media + text)
	std::string textSource; // gets the message; optional
	std::string message;
	std::string overflowMessage; // Follow, Subscriber: past kMaxQueued waiting
	std::string variantMessage;  // TwitchSub: a resub; Member: a milestone; Jewels: several gifts at once

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
	AlertSettings jewels{true, {}, {}, "{name} sent {gift}!", {}, "{name} sent {count} × {gift}!"};
	int minBits = 1;        // smaller cheers don't alert
	int minRaidViewers = 1; // nor smaller raids
	int minJewels = 1;      // nor smaller Jewels gifts
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

// The {placeholders} a kind's messages can use, e.g. {"{name}", "{amount}", "{message}"}; overflow: the "more than
// 10 wait" message, which has only {count}. Keep in step with what FormatAlert fills in for each kind's events.
std::vector<std::string> UsablePlaceholders(AlertKind kind, bool overflow);
// The {word}s in text the kind won't fill in (another kind's, or a typo), each once, in order. They would show
// as typed, or blank for another kind's: the settings window won't save until they're fixed.
std::vector<std::string> UnusablePlaceholders(std::string_view text, AlertKind kind, bool overflow);

std::string SerializeAlertsConfig(const AlertsConfig &config);
// Missing or malformed fields fall back to the defaults.
AlertsConfig ParseAlertsConfig(const std::string &json);

} // namespace stream_alerts
