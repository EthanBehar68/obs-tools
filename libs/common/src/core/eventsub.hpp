/*
obs-tools
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

#include <chrono>
#include <deque>
#include <initializer_list>
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

// Twitch EventSub over WebSocket: the message envelope and subscription requests, shared by every plugin.
namespace obs_tools::eventsub {

// Twitch sends a keepalive when nothing else happened for this long; no message for longer means the connection is
// gone. 10..600 are allowed: a minute keeps the thread asleep yet notices a silent drop quickly.
constexpr int kKeepaliveSeconds = 60;
constexpr const char *kUrl = "wss://eventsub.wss.twitch.tv/ws?keepalive_timeout_seconds=60";
constexpr const char *kSubscriptionsUrl = "https://api.twitch.tv/helix/eventsub/subscriptions";

struct Message {
	enum class Type { Welcome, Keepalive, Notification, Reconnect, Revocation, Other };
	Type type = Type::Other;
	std::string messageId;        // metadata.message_id: Twitch may send a message more than once
	std::string sessionId;        // welcome
	int keepaliveSeconds = 0;     // welcome
	std::string reconnectUrl;     // reconnect
	std::string subscriptionType; // notification, revocation
	std::string status;           // revocation: why, e.g. "authorization_revoked"
	nlohmann::json event;         // notification: payload.event
};
std::optional<Message> ParseMessage(const std::string &json);

// {"type","version","condition":{...},"transport":{"method":"websocket","session_id"}}
std::string BuildSubscription(std::string_view type, std::string_view version,
			      const std::vector<std::pair<std::string, std::string>> &condition,
			      const std::string &sessionId);

// The protocol of one EventSub session, without I/O, so it can be tested with Twitch's own messages: the welcome
// and subscribing, keepalive deadlines, duplicates, and a server-requested move to a new socket (the old socket is
// read until the new one says welcome; subscriptions move with it).
class Session {
public:
	using Clock = std::chrono::steady_clock;
	static constexpr auto kReconnectWindow = std::chrono::seconds(15); // for the new socket's welcome

	struct Step {
		std::string subscribe;   // the first welcome: subscribe on this session id within 10 s
		std::string reconnectTo; // open a second socket at this URL
		bool promote = false;    // that socket said welcome: switch to it, close the old one
		std::optional<Message> notification;
		std::optional<Message> revocation;
	};

	explicit Session(Clock::time_point now) : lastReceive_(now) {}

	// A message from the current socket, or (fromSuccessor) from the one opened for reconnectTo.
	Step Handle(const std::string &text, bool fromSuccessor, Clock::time_point now);
	// Twitch sends something at least every keepalive interval; silence past this means the connection is gone.
	Clock::time_point Deadline() const { return lastReceive_ + keepalive_ + std::chrono::seconds(10); }
	bool Welcomed() const { return welcomed_; }
	bool Reconnecting() const { return reconnecting_; }
	Clock::time_point ReconnectDeadline() const { return reconnectDeadline_; }
	// The second socket failed or took too long: the old one closes soon, and then the session ends.
	void AbandonReconnect() { reconnecting_ = false; }

private:
	bool Seen(const std::string &messageId);

	Clock::time_point lastReceive_;
	Clock::duration keepalive_ = std::chrono::seconds(kKeepaliveSeconds);
	Clock::time_point reconnectDeadline_{};
	bool welcomed_ = false;
	bool reconnecting_ = false;
	std::deque<std::string> recent_; // the last kRecentIds message ids, oldest first
	std::unordered_set<std::string> recentSet_;
	static constexpr size_t kRecentIds = 64;
};

// The string at a path of object keys, or empty when anything on the way is missing or not a string.
std::string StringAt(const nlohmann::json &obj, std::initializer_list<const char *> path);

} // namespace obs_tools::eventsub
