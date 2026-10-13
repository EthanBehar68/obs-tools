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

// A paid or membership event from YouTube live chat (Super Chats and Stickers, memberships, Jewels gifts). Only the chat carries these, so Unified Chat (which reads it)
// passes them on to Stream Alerts as JSON over a libobs signal.
struct YouTubeEvent {
	enum class Kind { SuperChat, SuperSticker, NewMember, MemberMilestone, GiftMemberships, Jewels };
	Kind kind = Kind::SuperChat;
	std::string id;        // the chat message's id; Jewels: the id and the combo, "<id>#<combo>" (see below)
	std::string channelId; // who
	std::string name;
	std::string amount;  // Super Chat / Sticker: "$5.00" (YouTube's display string)
	std::string message; // the viewer's comment; a sticker's description
	std::string level;   // membership level name, when YouTube gives it
	int64_t months = 0;  // MemberMilestone
	int64_t count = 0;   // GiftMemberships; Jewels: gifts new in this event
	// Jewels: a gift (a "giftEvent" in YouTube's terms). In a combo YouTube sends the same message again under the
	// same id with a higher comboCount; each such update becomes one event for the gifts it adds (count).
	std::string gift;   // the gift's name, e.g. "Rose"
	int64_t combo = 0;  // gifts in the combo so far (1 when not a combo)
	int64_t jewels = 0; // Jewels per gift, as YouTube reports jewelsAmount
};

// The Jewels an event's new gifts are worth. YouTube documents jewelsAmount as "the amount of Jewels redeemed for
// the gift", read here as per gift; if a log shows it's the combo's running total, change this one place.
inline int64_t JewelsTotal(const YouTubeEvent &event)
{
	return event.jewels * event.count;
}

std::string SerializeYouTubeEvent(const YouTubeEvent &event);
std::optional<YouTubeEvent> ParseYouTubeEvent(const std::string &json);

} // namespace obs_tools
