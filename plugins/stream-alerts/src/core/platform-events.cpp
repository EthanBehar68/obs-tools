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

#include "platform-events.hpp"
#include "core/eventsub.hpp"

#include <algorithm>
#include <json.hpp>

using json = nlohmann::json;

namespace stream_alerts {

using unified_chat::eventsub::StringAt;

std::string BuildFollowSubscription(const std::string &sessionId, const std::string &userId)
{
	return unified_chat::eventsub::BuildSubscription(
		"channel.follow", "2", {{"broadcaster_user_id", userId}, {"moderator_user_id", userId}}, sessionId);
}

std::optional<AlertEvent> FollowFromEvent(const json &event)
{
	AlertEvent follow{AlertKind::Follow, StringAt(event, {"user_id"}), StringAt(event, {"user_name"})};
	if (follow.name.empty())
		follow.name = StringAt(event, {"user_login"});
	if (follow.id.empty())
		return std::nullopt;
	return follow;
}

bool TwitchUser::HasScope(const std::string &scope) const
{
	return std::find(scopes.begin(), scopes.end(), scope) != scopes.end();
}

std::optional<TwitchUser> ParseValidate(const std::string &text)
{
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;
	TwitchUser user;
	user.id = StringAt(obj, {"user_id"});
	if (user.id.empty())
		return std::nullopt;
	auto scopes = obj.find("scopes");
	if (scopes != obj.end() && scopes->is_array()) {
		for (const auto &scope : *scopes) {
			if (scope.is_string())
				user.scopes.push_back(scope.get<std::string>());
		}
	}
	return user;
}

std::string ParseHelixMessage(const std::string &text)
{
	json obj = json::parse(text, nullptr, false);
	return obj.is_object() ? StringAt(obj, {"message"}) : std::string();
}

std::string RecentSubscribersUrl()
{
	return "https://www.googleapis.com/youtube/v3/subscriptions?part=subscriberSnippet&myRecentSubscribers=true"
	       "&maxResults=50&fields=items(subscriberSnippet(channelId,title))";
}

std::optional<std::vector<Subscriber>> ParseRecentSubscribers(const std::string &text)
{
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;
	std::vector<Subscriber> subscribers;
	auto items = obj.find("items");
	if (items == obj.end())
		return subscribers; // no subscribers yet: fields= drops the empty list
	if (!items->is_array())
		return std::nullopt;
	for (const auto &item : *items) {
		Subscriber subscriber{StringAt(item, {"subscriberSnippet", "channelId"}),
				      StringAt(item, {"subscriberSnippet", "title"})};
		if (!subscriber.channelId.empty())
			subscribers.push_back(std::move(subscriber));
	}
	return subscribers;
}

std::vector<Subscriber> SubscriberWatch::Update(const std::vector<Subscriber> &newestFirst)
{
	std::vector<Subscriber> fresh;
	for (auto it = newestFirst.rbegin(); it != newestFirst.rend(); ++it) {
		if (seen_.insert(it->channelId).second && primed_)
			fresh.push_back(*it);
	}
	primed_ = true;
	return fresh;
}

} // namespace stream_alerts
