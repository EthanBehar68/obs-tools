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

#include "obs-tools.hpp"

#include "chat-message.hpp"

#include <array>
#include <string>
#include <string_view>

namespace unified_chat {

// Lines that stand out from chat: alerts (follows, subs, cheers, Super Chats...), first-time chatters, and
// messages that mention you. Each kind has a colour and an emoji the user can change (Settings > Highlights).
enum class HighlightKind {
	Follow,
	TwitchSub,
	GiftSubs,
	Cheer,
	Raid,
	Subscriber, // YouTube channel subscriber
	SuperChat,  // Super Chats and Super Stickers
	Member,
	GiftMemberships,
	FirstChatter,
	Mention,
	Count,
};
constexpr size_t kHighlightKinds = (size_t)HighlightKind::Count;

const char *HighlightKey(HighlightKind kind); // JSON key and locale suffix, e.g. "gift_subs"

struct Highlight {
	bool show = true;
	std::string color; // "#rrggbb" accent; empty: the theme's own tint (the mention default)
	std::string icon;  // any emoji, shown before the line's text (or a first-time chatter's name); may be empty
};

struct Highlights {
	std::array<Highlight, kHighlightKinds> kinds;
	Highlights();
	Highlight &operator[](HighlightKind kind) { return kinds[(size_t)kind]; }
	const Highlight &operator[](HighlightKind kind) const { return kinds[(size_t)kind]; }
};
Highlight DefaultHighlight(HighlightKind kind);

// Trimmed, and at most kMaxIconBytes (one emoji, joined ones such as flags or families included), cut at a
// character boundary, so an icon can't turn into a sentence.
constexpr size_t kMaxIconBytes = 32;
std::string SanitizeIcon(std::string_view icon);

// How a highlighted line looks on the chat background: a tint of the accent, and the accent made readable on that
// tint (lightness only, like name colours). Worked out when settings or the theme change, not per message.
struct HighlightStyle {
	std::string background; // "#rrggbb"
	std::string text;       // "#rrggbb"
};
// Empty fields when accent or background isn't "#rrggbb".
HighlightStyle ComputeHighlightStyle(std::string_view accent, std::string_view background);

// An alert shown in the chat: from Stream Alerts (follows, YouTube subscribers), Twitch chat (subs, gifts, raids,
// cheers) or YouTube chat (Super Chats, memberships). text is what happened, comment the viewer's own words.
struct ChatEvent {
	Platform platform = Platform::Twitch;
	HighlightKind kind = HighlightKind::Follow;
	std::string text;
	std::string comment;
};

// The event line for a YouTube Super Chat / Sticker / membership message, or a Twitch cheer; nullopt for any other
// message.
std::optional<ChatEvent> EventFromMessage(const ChatMessage &message);

} // namespace unified_chat
