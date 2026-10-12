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

#include <initializer_list>
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Twitch EventSub over WebSocket: the message envelope and subscription requests, shared by every plugin.
namespace unified_chat::eventsub {

// Twitch sends a keepalive when nothing else happened for this long; no message for longer means the connection is
// gone. 10..600 are allowed: a minute keeps the thread asleep yet notices a silent drop quickly.
constexpr int kKeepaliveSeconds = 60;
constexpr const char *kUrl = "wss://eventsub.wss.twitch.tv/ws?keepalive_timeout_seconds=60";
constexpr const char *kSubscriptionsUrl = "https://api.twitch.tv/helix/eventsub/subscriptions";

struct Message {
	enum class Type { Welcome, Keepalive, Notification, Reconnect, Revocation, Other };
	Type type = Type::Other;
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

// The string at a path of object keys, or empty when anything on the way is missing or not a string.
std::string StringAt(const nlohmann::json &obj, std::initializer_list<const char *> path);

} // namespace unified_chat::eventsub
