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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace unified_chat {

enum class Platform { Twitch, YouTube };

// A Twitch emote in a message: emote id and its inclusive range, in Unicode characters (code points).
struct EmoteRange {
	std::string id;
	int start = 0;
	int end = 0;
};

// A Twitch chat badge, e.g. {"moderator", "1"} or {"subscriber", "12"}.
struct Badge {
	std::string set;
	std::string version;
};

struct ChatMessage {
	Platform platform = Platform::Twitch;
	std::string id;
	std::string author;
	std::string text;
	std::string color; // "#RRGGBB" or empty
	bool isAction = false;
	bool isSelf = false;
	uint64_t sendId = 0;  // set on local echoes of a message sent from the dock
	int64_t postedAt = 0; // unix seconds when the platform says it was posted; 0 = unknown
	// New fields go at the end so positional initialization elsewhere keeps its meaning.
	std::string mention;            // what to type after '@' to mention the author: Twitch login, YouTube handle
	std::string replyTo;            // for a reply, the display name of the message it answers
	std::string authorId;           // Twitch user-id, YouTube channel ID; lets a ban find the author's lines
	std::vector<EmoteRange> emotes; // Twitch emotes from the IRC tag
	std::vector<Badge> badges;      // role badges to show, in display order
};

// A moderator removed something from chat. The view strikes the affected lines through and, except for a
// single deleted message, adds a notice.
struct ModerationEvent {
	enum class Kind {
		DeleteMessage, // one message (messageId)
		RemoveUser,    // timeout (durationSeconds > 0) or ban: all of that user's messages
		ClearChat,     // everything on this platform
	};
	Platform platform = Platform::Twitch;
	Kind kind = Kind::DeleteMessage;
	std::string messageId;
	std::string userLogin; // Twitch login (matches ChatMessage::mention)
	std::string userId;    // matches ChatMessage::authorId
	std::string userName;  // for the notice
	int64_t durationSeconds = 0;
	std::string eventId;  // YouTube: the event's own id (dedup)
	int64_t postedAt = 0; // YouTube: unix seconds (history cutoff); 0 = unknown
};

inline std::string_view PlatformName(Platform platform)
{
	return platform == Platform::Twitch ? "Twitch" : "YouTube";
}

} // namespace unified_chat
