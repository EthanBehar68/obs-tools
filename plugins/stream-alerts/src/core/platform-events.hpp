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

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

// Twitch EventSub over WebSocket (channel.follow v2) and YouTube's recent subscribers feed.
namespace stream_alerts {

// Twitch sends a keepalive when nothing else happened for this long; no message for longer means the connection is
// gone. 10..600 are allowed: a minute keeps the thread asleep yet notices a silent drop quickly.
constexpr int kKeepaliveSeconds = 60;
constexpr const char *kEventSubUrl = "wss://eventsub.wss.twitch.tv/ws?keepalive_timeout_seconds=60";
constexpr const char *kSubscriptionsUrl = "https://api.twitch.tv/helix/eventsub/subscriptions";
constexpr const char *kFollowScope = "moderator:read:followers";

struct EventSubMessage {
	enum class Type { Welcome, Keepalive, Notification, Reconnect, Revocation, Other };
	Type type = Type::Other;
	std::string sessionId;        // welcome
	int keepaliveSeconds = 0;     // welcome
	std::string reconnectUrl;     // reconnect
	std::string subscriptionType; // notification, revocation
	std::string status;           // revocation: why, e.g. "authorization_revoked"
	std::string userId, userName; // channel.follow: who followed
};
std::optional<EventSubMessage> ParseEventSubMessage(const std::string &json);
// Follows of the signed-in account's own channel (it is its own moderator).
std::string BuildFollowSubscription(const std::string &sessionId, const std::string &userId);

struct TwitchUser {
	std::string id;
	std::vector<std::string> scopes;
	bool HasScope(const std::string &scope) const;
};
// id.twitch.tv/oauth2/validate
std::optional<TwitchUser> ParseValidate(const std::string &json);
// Helix {"error":"Forbidden","status":403,"message":"..."}: the message, or empty.
std::string ParseHelixMessage(const std::string &json);

// YouTube subscriptions.list?myRecentSubscribers=true: newest first; 1 quota unit per request.
std::string RecentSubscribersUrl();
struct Subscriber {
	std::string channelId;
	std::string name;
};
std::optional<std::vector<Subscriber>> ParseRecentSubscribers(const std::string &json);

// Tells new subscribers from ones already there. The first list is the baseline (no alerts: those subscribed before
// the stream), then each later list yields who is new, oldest first.
class SubscriberWatch {
public:
	std::vector<Subscriber> Update(const std::vector<Subscriber> &newestFirst);

private:
	bool primed_ = false;
	std::unordered_set<std::string> seen_;
};

} // namespace stream_alerts
