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

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unified_chat::twitch {

constexpr const char *kIrcUrl = "https://irc.chat.twitch.tv:6697";
constexpr size_t kMaxMessageLength = 500;
constexpr const char *kValidateUrl = "https://id.twitch.tv/oauth2/validate";

struct IrcMessage {
	std::map<std::string, std::string, std::less<>> tags;
	std::string prefix;
	std::string command;
	std::vector<std::string> params;

	// Both refer into this message; no copies.
	std::string_view Nick() const;
	const std::string &Tag(std::string_view key) const; // empty when absent
};

std::string UnescapeTagValue(std::string_view value);
std::optional<IrcMessage> ParseIrcLine(std::string_view line);

// Accepts "name", "#name" or a twitch.tv URL. Returns an empty string when the result is not a valid login.
std::string NormalizeChannel(std::string_view input);

// Returns the login from an https://id.twitch.tv/oauth2/validate response, or an empty string.
std::string ParseValidateLogin(const std::string &json);

// Splits a raw byte stream into complete IRC lines, keeping any partial line for the next call.
class LineBuffer {
public:
	std::vector<std::string> Append(std::string_view data);
	void Clear() { pending_.clear(); }

private:
	std::string pending_;
};

struct SessionOutput {
	std::vector<std::string> outgoing;
	std::vector<ChatMessage> messages;
	std::vector<ModerationEvent> moderation;
	std::vector<std::string> notices;
	std::string channelId; // set once the channel's numeric id is known (ROOMSTATE)
	bool reconnect = false;
	bool authFailed = false;
};

// Protocol state for one IRC connection. Has no I/O so it can be driven directly by tests.
class IrcSession {
public:
	IrcSession(std::string channel, std::string login, std::string token, unsigned anonymousSuffix = 12345);

	std::vector<std::string> Start();
	void HandleLine(std::string_view line, SessionOutput &out);

	bool IsAuthenticated() const { return !token_.empty(); }
	bool IsJoined() const { return joined_; }
	bool CanSend() const { return IsAuthenticated() && joined_; }
	const std::string &Channel() const { return channel_; }

	// With replyParentId (the id tag of a received message), sends a threaded Twitch reply to it.
	std::optional<std::string> BuildPrivmsg(std::string_view text, std::string_view replyParentId = {}) const;
	ChatMessage LocalEcho(std::string_view text, std::string_view replyTo = {}) const;

private:
	std::string channel_;
	std::string login_;
	std::string token_;
	std::string nick_;
	std::string displayName_;
	std::string color_;
	std::string channelId_;
	std::vector<Badge> badges_;
	bool joined_ = false;
};

} // namespace unified_chat::twitch
