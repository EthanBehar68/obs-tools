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

#pragma once

#include "chat-message.hpp"

#include <optional>
#include <string>
#include <vector>

namespace unified_chat {

// Scopes the Twitch sign-in asks for so the dock can moderate (Ban/Unban User, Delete Chat Messages).
constexpr const char *kTwitchModerationScopes[] = {"moderator:manage:banned_users", "moderator:manage:chat_messages"};
constexpr int64_t kTwitchMaxTimeoutSeconds = 1209600; // 14 days

// An HTTP request described without doing any I/O, so the builders can be tested.
struct HttpRequest {
	enum class Method { Get, Post, Delete };
	Method method = Method::Get;
	std::string url;
	std::string body; // JSON for Post
};

// https://id.twitch.tv/oauth2/validate response.
struct TwitchIdentity {
	std::string login;
	std::string userId;
	std::vector<std::string> scopes;
	bool CanModerate() const;
};
std::optional<TwitchIdentity> ParseTwitchIdentity(const std::string &json);

// Helix moderation calls. broadcasterId is the channel's id, moderatorId the signed-in user's.
// Returns nullopt for an action that doesn't apply (e.g. a DeleteMessage without a message id).
std::optional<HttpRequest> BuildTwitchModeration(const ModerationAction &action, const std::string &broadcasterId,
						 const std::string &moderatorId);

// YouTube Data API moderation calls. Unban needs the ban's id from the Ban/Timeout response.
std::optional<HttpRequest> BuildYouTubeModeration(const ModerationAction &action, const std::string &liveChatId,
						  const std::string &banId = {});
std::string ParseYouTubeBanId(const std::string &json);

// A short human-readable reason from a Twitch or Google error body, or empty.
std::string ModerationErrorMessage(const std::string &json);

} // namespace unified_chat
