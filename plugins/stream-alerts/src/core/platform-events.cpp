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

using obs_tools::eventsub::StringAt;

std::string BuildFollowSubscription(const std::string &sessionId, const std::string &userId)
{
	return obs_tools::eventsub::BuildSubscription(
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

std::vector<std::string> BuildTwitchSubscriptions(const std::string &sessionId, const std::string &userId,
						  const TwitchWants &wants)
{
	std::vector<std::string> bodies;
	if (wants.follows)
		bodies.push_back(BuildFollowSubscription(sessionId, userId));
	if (wants.notifications)
		bodies.push_back(obs_tools::eventsub::BuildSubscription(
			"channel.chat.notification", "1", {{"broadcaster_user_id", userId}, {"user_id", userId}},
			sessionId));
	if (wants.cheers)
		bodies.push_back(obs_tools::eventsub::BuildSubscription("channel.cheer", "1",
									{{"broadcaster_user_id", userId}}, sessionId));
	return bodies;
}

static int64_t IntAt(const json &obj, const char *section, const char *key)
{
	auto it = obj.find(section);
	if (it == obj.end() || !it->is_object())
		return 0;
	auto value = it->find(key);
	return value != it->end() && value->is_number_integer() ? value->get<int64_t>() : 0;
}

static std::string ChatterName(const json &event)
{
	auto anonymous = event.find("chatter_is_anonymous");
	if (anonymous != event.end() && anonymous->is_boolean() && anonymous->get<bool>())
		return "Anonymous";
	std::string name = StringAt(event, {"chatter_user_name"});
	return name.empty() ? StringAt(event, {"chatter_user_login"}) : name;
}

std::optional<AlertEvent> NotificationFromEvent(const json &event)
{
	const std::string type = StringAt(event, {"notice_type"});
	AlertEvent alert;
	alert.id = StringAt(event, {"message_id"});
	alert.name = ChatterName(event);
	alert.message = StringAt(event, {"message", "text"});
	if (type == "sub") {
		alert.kind = AlertKind::TwitchSub;
		alert.months = 1;
	} else if (type == "resub") {
		alert.kind = AlertKind::TwitchSub;
		alert.variant = true;
		alert.months = IntAt(event, "resub", "cumulative_months");
	} else if (type == "sub_gift") {
		// Part of a gift bomb: the community_sub_gift notice already alerts once for all of them.
		if (!StringAt(event, {"sub_gift", "community_gift_id"}).empty())
			return std::nullopt;
		alert.kind = AlertKind::GiftSubs;
		alert.count = 1;
	} else if (type == "community_sub_gift") {
		alert.kind = AlertKind::GiftSubs;
		alert.count = IntAt(event, "community_sub_gift", "total");
	} else if (type == "raid") {
		alert.kind = AlertKind::Raid;
		alert.name = StringAt(event, {"raid", "user_name"});
		if (alert.name.empty())
			alert.name = StringAt(event, {"raid", "user_login"});
		alert.viewers = IntAt(event, "raid", "viewer_count");
		alert.message.clear();
	} else {
		return std::nullopt;
	}
	return alert;
}

std::optional<AlertEvent> CheerFromEvent(const json &event)
{
	auto bits = event.find("bits");
	if (bits == event.end() || !bits->is_number_integer())
		return std::nullopt;
	AlertEvent alert;
	alert.kind = AlertKind::Cheer;
	auto anonymous = event.find("is_anonymous");
	const bool isAnonymous = anonymous != event.end() && anonymous->is_boolean() && anonymous->get<bool>();
	alert.id = StringAt(event, {"user_id"});
	alert.name = isAnonymous ? "Anonymous" : StringAt(event, {"user_name"});
	if (alert.name.empty())
		alert.name = StringAt(event, {"user_login"});
	alert.count = bits->get<int64_t>();
	alert.amount = std::to_string(alert.count);
	alert.message = StringAt(event, {"message"});
	return alert;
}

AlertEvent FromYouTubeEvent(const obs_tools::YouTubeEvent &event)
{
	using Kind = obs_tools::YouTubeEvent::Kind;
	AlertEvent alert;
	alert.id = event.id; // the chat message's id: the queue drops a repeat
	alert.name = event.name;
	alert.amount = event.amount;
	alert.message = event.message;
	alert.level = event.level;
	alert.months = event.months;
	alert.count = event.count;
	switch (event.kind) {
	case Kind::SuperChat:
	case Kind::SuperSticker:
		alert.kind = AlertKind::SuperChat;
		break;
	case Kind::NewMember:
		alert.kind = AlertKind::Member;
		break;
	case Kind::MemberMilestone:
		alert.kind = AlertKind::Member;
		alert.variant = true;
		break;
	default:
		alert.kind = AlertKind::GiftMemberships;
	}
	return alert;
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
