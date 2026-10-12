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

#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Twitch chat over EventSub (reading) and the Helix API (sending), as Twitch recommends over IRC.
namespace unified_chat::twitch {

constexpr const char *kSendChatUrl = "https://api.twitch.tv/helix/chat/messages";
constexpr const char *kUsersUrl = "https://api.twitch.tv/helix/users";
// Without both, chat stays on IRC until the user signs in again.
constexpr const char *kEventSubChatScopes[] = {"user:read:chat", "user:write:chat"};
// channel.ban tells timeouts from bans; only the broadcaster can grant it.
constexpr const char *kBanEventsScope = "channel:moderate";

// One subscription request body per chat event listened to. withBanEvents adds channel.ban (the signed-in user
// is the broadcaster); then clear_user_messages is left out, since channel.ban says more about the same removal.
std::vector<std::string> BuildChatSubscriptions(const std::string &sessionId, const std::string &broadcasterId,
						const std::string &userId, bool withBanEvents);

// Helix Get Users.
struct HelixUser {
	std::string id;
	std::string login;
	std::string displayName;
};
std::vector<HelixUser> ParseUsers(const std::string &json);

// channel.chat.message -> a chat line; isSelf when the chatter is selfUserId. nullopt without a message id.
std::optional<ChatMessage> ChatMessageFromEvent(const nlohmann::json &event, std::string_view selfUserId);

// channel.chat.message_delete, channel.chat.clear, channel.chat.clear_user_messages or channel.ban. A cleared
// user doesn't say whether it was a timeout or a ban: durationSeconds is kRemovalUnknown.
std::optional<ModerationEvent> ModerationFromEvent(std::string_view type, const nlohmann::json &event);

// channel.chat.notification (subs, gifts, raids, announcements, ...): the line Twitch shows, then the viewer's own
// message if any, as the dock's notice. Empty when there's nothing to show.
std::string NotificationText(const nlohmann::json &event);

// Send Chat Message. nullopt when there's nothing to send.
std::optional<std::string> BuildSendChatBody(const std::string &broadcasterId, const std::string &senderId,
					     std::string_view text, std::string_view replyParentId = {});
struct SendResult {
	bool sent = false;
	std::string messageId;
	std::string dropReason; // why Twitch didn't send it, when it says
};
std::optional<SendResult> ParseSendChatResponse(const std::string &json);

} // namespace unified_chat::twitch
