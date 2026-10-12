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
#include "core/youtube-events.hpp"

#include <json.hpp>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

// Twitch follows (EventSub channel.follow v2, see core/eventsub.hpp) and YouTube's recent subscribers feed.
namespace stream_alerts {

constexpr const char *kFollowScope = "moderator:read:followers";

// Follows of the signed-in account's own channel (it is its own moderator): channel.follow v2.
std::string BuildFollowSubscription(const std::string &sessionId, const std::string &userId);
// A channel.follow event: who followed. nullopt without a user id.
std::optional<AlertEvent> FollowFromEvent(const nlohmann::json &event);

constexpr const char *kChatScope = "user:read:chat"; // channel.chat.notification: subs, gifts, raids
constexpr const char *kBitsScope = "bits:read";      // channel.cheer

// The EventSub subscriptions for what the Twitch listener should report, in the signed-in user's own channel.
struct TwitchWants {
	bool follows = false;
	bool notifications = false; // subs, gift subs, raids
	bool cheers = false;
};
std::vector<std::string> BuildTwitchSubscriptions(const std::string &sessionId, const std::string &userId,
						  const TwitchWants &wants);

// channel.chat.notification: a sub, resub, gift (a gift bomb once, not per recipient) or raid; nullopt for other
// notices.
std::optional<AlertEvent> NotificationFromEvent(const nlohmann::json &event);
// channel.cheer
std::optional<AlertEvent> CheerFromEvent(const nlohmann::json &event);
// What Unified Chat passes on from YouTube chat.
AlertEvent FromYouTubeEvent(const unified_chat::YouTubeEvent &event);

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
