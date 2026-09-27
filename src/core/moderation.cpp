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

#include "moderation.hpp"
#include "text-util.hpp"

#include <algorithm>
#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat {

static constexpr const char *kHelixModeration = "https://api.twitch.tv/helix/moderation";
static constexpr const char *kYouTubeApi = "https://www.googleapis.com/youtube/v3";

bool TwitchIdentity::CanModerate() const
{
	for (const char *needed : kTwitchModerationScopes) {
		if (std::find(scopes.begin(), scopes.end(), needed) == scopes.end())
			return false;
	}
	return true;
}

std::optional<TwitchIdentity> ParseTwitchIdentity(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;
	TwitchIdentity identity;
	identity.login = obj.value("login", std::string());
	identity.userId = obj.value("user_id", std::string());
	if (auto scopes = obj.find("scopes"); scopes != obj.end() && scopes->is_array()) {
		for (const auto &scope : *scopes) {
			if (scope.is_string())
				identity.scopes.push_back(scope.get<std::string>());
		}
	}
	if (identity.userId.empty())
		return std::nullopt;
	return identity;
}

std::optional<HttpRequest> BuildTwitchModeration(const ModerationAction &action, const std::string &broadcasterId,
						 const std::string &moderatorId)
{
	if (broadcasterId.empty() || moderatorId.empty())
		return std::nullopt;
	const std::string ids =
		"?broadcaster_id=" + UrlEncode(broadcasterId) + "&moderator_id=" + UrlEncode(moderatorId);

	switch (action.kind) {
	case ModerationAction::Kind::DeleteMessage:
		if (action.messageId.empty())
			return std::nullopt;
		return HttpRequest{HttpRequest::Method::Delete,
				   std::string(kHelixModeration) + "/chat" + ids +
					   "&message_id=" + UrlEncode(action.messageId),
				   {}};
	case ModerationAction::Kind::Unban:
		if (action.userId.empty())
			return std::nullopt;
		return HttpRequest{HttpRequest::Method::Delete,
				   std::string(kHelixModeration) + "/bans" + ids +
					   "&user_id=" + UrlEncode(action.userId),
				   {}};
	case ModerationAction::Kind::Timeout:
	case ModerationAction::Kind::Ban: {
		if (action.userId.empty())
			return std::nullopt;
		json data = {{"user_id", action.userId}};
		if (action.kind == ModerationAction::Kind::Timeout)
			data["duration"] = std::clamp<int64_t>(action.durationSeconds, 1, kTwitchMaxTimeoutSeconds);
		if (!action.reason.empty())
			data["reason"] = action.reason.substr(0, 500);
		return HttpRequest{HttpRequest::Method::Post, std::string(kHelixModeration) + "/bans" + ids,
				   json{{"data", data}}.dump()};
	}
	}
	return std::nullopt;
}

std::optional<HttpRequest> BuildYouTubeModeration(const ModerationAction &action, const std::string &liveChatId,
						  const std::string &banId)
{
	switch (action.kind) {
	case ModerationAction::Kind::DeleteMessage:
		if (action.messageId.empty())
			return std::nullopt;
		return HttpRequest{HttpRequest::Method::Delete,
				   std::string(kYouTubeApi) + "/liveChat/messages?id=" + UrlEncode(action.messageId),
				   {}};
	case ModerationAction::Kind::Unban:
		if (banId.empty())
			return std::nullopt; // only bans made from the dock can be lifted
		return HttpRequest{HttpRequest::Method::Delete,
				   std::string(kYouTubeApi) + "/liveChat/bans?id=" + UrlEncode(banId),
				   {}};
	case ModerationAction::Kind::Timeout:
	case ModerationAction::Kind::Ban: {
		if (action.userId.empty() || liveChatId.empty())
			return std::nullopt;
		json snippet = {{"liveChatId", liveChatId}, {"bannedUserDetails", {{"channelId", action.userId}}}};
		if (action.kind == ModerationAction::Kind::Timeout) {
			snippet["type"] = "temporary";
			snippet["banDurationSeconds"] = std::max<int64_t>(action.durationSeconds, 1);
		} else {
			snippet["type"] = "permanent";
		}
		return HttpRequest{HttpRequest::Method::Post, std::string(kYouTubeApi) + "/liveChat/bans?part=snippet",
				   json{{"snippet", snippet}}.dump()};
	}
	}
	return std::nullopt;
}

std::string ParseYouTubeBanId(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	return obj.is_object() ? obj.value("id", std::string()) : std::string();
}

std::string ModerationErrorMessage(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return {};
	// Twitch: {"error":"Bad Request","status":400,"message":"The user specified in the user_id field is already banned."}
	if (auto message = obj.find("message"); message != obj.end() && message->is_string())
		return message->get<std::string>();
	// Google: {"error":{"code":403,"message":"...","errors":[{"reason":"forbidden"}]}}
	if (auto error = obj.find("error"); error != obj.end() && error->is_object()) {
		if (auto message = error->find("message"); message != error->end() && message->is_string())
			return message->get<std::string>();
	}
	return {};
}

} // namespace unified_chat
