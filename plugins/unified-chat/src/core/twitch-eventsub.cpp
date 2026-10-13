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

#include "twitch-eventsub.hpp"
#include "emotes.hpp"
#include "twitch-irc.hpp"
#include "core/eventsub.hpp"
#include "core/text-util.hpp"

using json = nlohmann::json;
using unified_chat::eventsub::StringAt;

namespace unified_chat::twitch {

static constexpr std::string_view kActionPrefix = "\x01"
						  "ACTION ";

std::vector<std::string> BuildChatSubscriptions(const std::string &sessionId, const std::string &broadcasterId,
						const std::string &userId, bool withBanEvents)
{
	const std::vector<std::pair<std::string, std::string>> chat{{"broadcaster_user_id", broadcasterId},
								    {"user_id", userId}};
	std::vector<std::string> bodies;
	for (const char *type :
	     {"channel.chat.message", "channel.chat.message_delete", "channel.chat.clear", "channel.chat.notification"})
		bodies.push_back(eventsub::BuildSubscription(type, "1", chat, sessionId));
	if (withBanEvents)
		bodies.push_back(eventsub::BuildSubscription("channel.ban", "1",
							     {{"broadcaster_user_id", broadcasterId}}, sessionId));
	else
		bodies.push_back(eventsub::BuildSubscription("channel.chat.clear_user_messages", "1", chat, sessionId));
	return bodies;
}

std::vector<HelixUser> ParseUsers(const std::string &text)
{
	std::vector<HelixUser> users;
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object() || !obj.contains("data") || !obj["data"].is_array())
		return users;
	for (const auto &item : obj["data"]) {
		HelixUser user{StringAt(item, {"id"}), StringAt(item, {"login"}), StringAt(item, {"display_name"})};
		if (!user.id.empty())
			users.push_back(std::move(user));
	}
	return users;
}

// Text removed from the front: emote ranges move back with it, and any inside it go.
static void DropFront(std::vector<EmoteRange> &emotes, int codePoints)
{
	std::vector<EmoteRange> kept;
	for (auto range : emotes) {
		range.start -= codePoints;
		range.end -= codePoints;
		if (range.start >= 0)
			kept.push_back(std::move(range));
	}
	emotes = std::move(kept);
}

std::optional<ChatMessage> ChatMessageFromEvent(const json &event, std::string_view selfUserId)
{
	ChatMessage chat;
	chat.platform = Platform::Twitch;
	chat.id = StringAt(event, {"message_id"});
	if (chat.id.empty())
		return std::nullopt;
	chat.mention = StringAt(event, {"chatter_user_login"});
	chat.author = StringAt(event, {"chatter_user_name"});
	if (chat.author.empty())
		chat.author = chat.mention;
	chat.authorId = StringAt(event, {"chatter_user_id"});
	chat.color = SanitizeColor(StringAt(event, {"color"}));
	chat.isSelf = !selfUserId.empty() && chat.authorId == selfUserId;
	chat.firstMessage = StringAt(event, {"message_type"}) == "user_intro"; // Twitch's first-time chatter prompt
	if (auto cheer = event.find("cheer"); cheer != event.end() && cheer->is_object()) {
		auto bits = cheer->find("bits");
		if (bits != cheer->end() && bits->is_number_integer())
			chat.bits = bits->get<int64_t>();
	}

	// Fragments are the text in order, with emotes marked: their ranges in code points, like the IRC tag.
	const json *fragments = nullptr;
	if (auto message = event.find("message"); message != event.end() && message->is_object()) {
		if (auto found = message->find("fragments"); found != message->end() && found->is_array())
			fragments = &*found;
	}
	if (fragments) {
		int position = 0;
		for (const auto &fragment : *fragments) {
			const std::string text = StringAt(fragment, {"text"});
			const int length = (int)Utf8Length(text);
			const std::string emoteId = StringAt(fragment, {"emote", "id"});
			if (StringAt(fragment, {"type"}) == "emote" && !emoteId.empty() && length > 0)
				chat.emotes.push_back({emoteId, position, position + length - 1});
			chat.text += text;
			position += length;
		}
	} else {
		chat.text = StringAt(event, {"message", "text"});
	}

	// /me: message.text keeps IRC's "\x01ACTION ...\x01" wrapper, while the fragments are the plain text (as in the
	// example on Twitch's developer forum; not in the reference). Either form is read.
	if (StringAt(event, {"message", "text"}).rfind(kActionPrefix, 0) == 0)
		chat.isAction = true;
	if (chat.text.rfind(kActionPrefix, 0) == 0) {
		chat.isAction = true;
		chat.text.erase(0, kActionPrefix.size());
		DropFront(chat.emotes, (int)kActionPrefix.size());
	}
	if (chat.isAction && !chat.text.empty() && chat.text.back() == '\x01')
		chat.text.pop_back();

	auto badges = event.find("badges");
	if (badges != event.end() && badges->is_array()) {
		std::string tag; // the IRC form, "set/id,set/id"
		for (const auto &badge : *badges) {
			const std::string set = StringAt(badge, {"set_id"}), version = StringAt(badge, {"id"});
			if (!set.empty())
				tag += (tag.empty() ? "" : ",") + set + "/" + version;
		}
		chat.badges = ParseRoleBadges(tag);
	}

	if (const std::string parentLogin = StringAt(event, {"reply", "parent_user_login"}); !parentLogin.empty()) {
		const std::string parentName = StringAt(event, {"reply", "parent_user_name"});
		chat.replyTo = parentName.empty() ? parentLogin : parentName;
		const size_t before = Utf8Length(chat.text);
		DropLeadingMention(chat.text, parentLogin, parentName);
		DropFront(chat.emotes, (int)(before - Utf8Length(chat.text)));
	}
	return chat;
}

std::optional<ModerationEvent> ModerationFromEvent(std::string_view type, const json &event)
{
	ModerationEvent moderation;
	moderation.platform = Platform::Twitch;
	if (type == "channel.chat.message_delete") {
		moderation.kind = ModerationEvent::Kind::DeleteMessage;
		moderation.messageId = StringAt(event, {"message_id"});
		moderation.userLogin = StringAt(event, {"target_user_login"});
		moderation.userId = StringAt(event, {"target_user_id"});
		moderation.userName = StringAt(event, {"target_user_name"});
		if (moderation.messageId.empty())
			return std::nullopt;
	} else if (type == "channel.chat.clear") {
		moderation.kind = ModerationEvent::Kind::ClearChat;
	} else if (type == "channel.chat.clear_user_messages" || type == "channel.ban") {
		const bool ban = type == "channel.ban";
		moderation.kind = ModerationEvent::Kind::RemoveUser;
		moderation.userLogin = StringAt(event, {ban ? "user_login" : "target_user_login"});
		moderation.userId = StringAt(event, {ban ? "user_id" : "target_user_id"});
		moderation.userName = StringAt(event, {ban ? "user_name" : "target_user_name"});
		if (moderation.userName.empty())
			moderation.userName = moderation.userLogin;
		if (moderation.userLogin.empty() && moderation.userId.empty())
			return std::nullopt;
		moderation.durationSeconds = kRemovalUnknown;
		if (ban) {
			auto permanent = event.find("is_permanent");
			const int64_t from = ParseRfc3339(StringAt(event, {"banned_at"}));
			const int64_t to = ParseRfc3339(StringAt(event, {"ends_at"}));
			if (permanent != event.end() && permanent->is_boolean() && permanent->get<bool>())
				moderation.durationSeconds = 0;
			else if (from > 0 && to > from)
				moderation.durationSeconds = to - from;
		}
	} else {
		return std::nullopt;
	}
	return moderation;
}

std::string NotificationText(const json &event)
{
	std::string text = StringAt(event, {"system_message"});
	const std::string message = StringAt(event, {"message", "text"});
	if (!message.empty())
		text += text.empty() ? message : " - " + message;
	return text;
}

std::optional<ChatEvent> NotificationEvent(const json &event)
{
	const std::string type = StringAt(event, {"notice_type"});
	ChatEvent chat;
	chat.platform = Platform::Twitch;
	if (type == "sub" || type == "resub")
		chat.kind = HighlightKind::TwitchSub;
	else if (type == "sub_gift" || type == "community_sub_gift")
		chat.kind = HighlightKind::GiftSubs;
	else if (type == "raid")
		chat.kind = HighlightKind::Raid;
	else
		return std::nullopt;
	chat.text = StringAt(event, {"system_message"});
	chat.comment = StringAt(event, {"message", "text"});
	if (chat.text.empty())
		return std::nullopt;
	return chat;
}

std::optional<std::string> BuildSendChatBody(const std::string &broadcasterId, const std::string &senderId,
					     std::string_view text, std::string_view replyParentId)
{
	std::string message = Trim(StripLineBreaks(text));
	if (message.empty() || broadcasterId.empty() || senderId.empty())
		return std::nullopt;
	json body = {{"broadcaster_id", broadcasterId}, {"sender_id", senderId}, {"message", message}};
	if (!replyParentId.empty())
		body["reply_parent_message_id"] = std::string(replyParentId);
	return body.dump();
}

std::optional<SendResult> ParseSendChatResponse(const std::string &text)
{
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object() || !obj.contains("data") || !obj["data"].is_array() || obj["data"].empty())
		return std::nullopt;
	const json &data = obj["data"][0];
	if (!data.is_object())
		return std::nullopt;
	SendResult result;
	auto sent = data.find("is_sent");
	result.sent = sent != data.end() && sent->is_boolean() && sent->get<bool>();
	result.messageId = StringAt(data, {"message_id"});
	result.dropReason = StringAt(data, {"drop_reason", "message"});
	if (result.dropReason.empty())
		result.dropReason = StringAt(data, {"drop_reason", "code"});
	return result;
}

} // namespace unified_chat::twitch
