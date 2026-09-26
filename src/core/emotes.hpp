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
#include <string_view>
#include <unordered_map>
#include <vector>

namespace unified_chat {

enum class EmoteProvider { Twitch, BTTV, FFZ, SevenTV };

struct Emote {
	EmoteProvider provider = EmoteProvider::Twitch;
	std::string id;
	std::string name;
	double aspect = 1.0;   // width / height, so a line can be laid out before the image arrives
	bool animated = false; // BTTV/7TV: picks the small static file instead of the whole animation
};

// Twitch "emotes" tag, e.g. "25:0-4,12-16/1902:6-10". Malformed parts are skipped.
std::vector<EmoteRange> ParseTwitchEmotes(std::string_view tag);

// Third-party emote lists. Each returns an empty list for a response it can't read (e.g. a 404 body for a
// channel that doesn't use that service).
std::vector<Emote> ParseBttvEmotes(const std::string &json);    // global list, or a channel's
std::vector<Emote> ParseFfzEmotes(const std::string &json);     // /set/global or /room/id/<id>
std::vector<Emote> ParseSevenTvEmotes(const std::string &json); // /emote-sets/global or /users/twitch/<id>

// Emotes by name for word matching. Higher priority wins a name clash.
class EmoteIndex {
public:
	void Add(const std::vector<Emote> &emotes, int priority);
	const Emote *Find(std::string_view word) const;
	size_t Size() const { return byName_.size(); }
	void Clear() { byName_.clear(); }

private:
	struct Entry {
		Emote emote;
		int priority;
	};
	std::unordered_map<std::string, Entry> byName_;
};

// Name-clash priorities: a channel's own emotes over globals; among services 7TV, then BTTV, then FFZ.
int EmotePriority(EmoteProvider provider, bool channel);

struct MessageSegment {
	std::string text;           // plain text (empty for an emote)
	std::optional<Emote> emote; // or an emote
};

// Splits a message into text and emote pieces: Twitch emotes by their ranges (ignored altogether if they don't
// fit the text), then whole words found in the index. The index may be null.
std::vector<MessageSegment> SplitMessage(std::string_view text, const std::vector<EmoteRange> &twitchEmotes,
					 const EmoteIndex *index);

// Document resource name and download address of an emote image (static, 1x or 2x for high-DPI).
std::string EmoteImageKey(const Emote &emote);
std::string EmoteImageUrl(const Emote &emote, bool hiDpi);

// Twitch "badges" tag -> the role badges to show (broadcaster, moderator, VIP, founder / subscriber), in that
// order; other badges are dropped.
std::vector<Badge> ParseRoleBadges(std::string_view tag);
// Helix Get Global / Channel Chat Badges -> "set/version" -> image URL (1x, or 2x for high-DPI).
std::unordered_map<std::string, std::string> ParseBadgeImages(const std::string &json, bool hiDpi);
std::string BadgeKey(const Badge &badge);      // "set/version"
std::string BadgeImageKey(const Badge &badge); // document resource name

} // namespace unified_chat
