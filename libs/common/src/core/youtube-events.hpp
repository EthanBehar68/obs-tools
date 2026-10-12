/*
obs-tools
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
#include <optional>
#include <string>

namespace obs_tools {

// A paid or membership event from YouTube live chat. Only the chat carries these, so Unified Chat (which reads it)
// passes them on to Stream Alerts as JSON over a libobs signal.
struct YouTubeEvent {
	enum class Kind { SuperChat, SuperSticker, NewMember, MemberMilestone, GiftMemberships };
	Kind kind = Kind::SuperChat;
	std::string id;        // the chat message's id
	std::string channelId; // who
	std::string name;
	std::string amount;  // Super Chat / Sticker: "$5.00" (YouTube's display string)
	std::string message; // the viewer's comment; a sticker's description
	std::string level;   // membership level name, when YouTube gives it
	int64_t months = 0;  // MemberMilestone
	int64_t count = 0;   // GiftMemberships
};

std::string SerializeYouTubeEvent(const YouTubeEvent &event);
std::optional<YouTubeEvent> ParseYouTubeEvent(const std::string &json);

} // namespace obs_tools
