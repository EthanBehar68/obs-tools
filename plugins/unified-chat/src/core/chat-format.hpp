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

#include <string>
#include <string_view>
#include <vector>

namespace unified_chat {

// Image resource names registered on the chat view's QTextDocument.
constexpr const char *kTwitchIconResource = "unified-chat://icon/twitch";
constexpr const char *kYouTubeIconResource = "unified-chat://icon/youtube";
constexpr const char *kSelfBadgeResource = "unified-chat://icon/self";

std::string_view DefaultNameColor(Platform platform);
std::string_view IconResource(Platform platform);

class NameColorResolver;
class EmoteIndex;

// Renders one chat line as Qt rich text: platform icon, badges, display name, message.
// All user-controlled text is escaped.
std::string FormatMessageHtml(const ChatMessage &message, int iconSize);
// Same, with one icon per platform, for a message you sent to several platforms at once.
// With colors, the name color is made readable on the chat background. Twitch emotes (from the message) and
// words found in emotes become images emoteHeight tall. renderedTextLength receives the message text's length in
// the document (UTF-16 units, one per emote image).
std::string FormatMessageHtml(const ChatMessage &message, int iconSize, const std::vector<Platform> &platforms,
			      NameColorResolver *colors = nullptr, const EmoteIndex *emotes = nullptr,
			      int emoteHeight = 0, int *renderedTextLength = nullptr);
std::string FormatNoticeHtml(std::string_view text);

// How a line removed by a moderator is marked. A line keeps its most severe tag: banned > timed out >
// deleted / chat cleared.
struct ModerationTag {
	int severity;      // 1 deleted or chat cleared, 2 timed out, 3 banned
	std::string label; // "(deleted)", "(timed out 10 minutes)", ...
	std::string color; // "#rrggbb"
};
ModerationTag TagFor(const ModerationEvent &event);
// The notice shown when it happens ("Twitch: dezad was timed out for 10 minutes"); empty for a single deleted
// message, which only gets its tag.
std::string ModerationNotice(const ModerationEvent &event, std::string_view name);

// Colors used by the view and the legend.
constexpr const char *kDimmedTextColor = "#a0a0a0";
constexpr const char *kTagGreyColor = "#9a9a9a";
constexpr const char *kTagTimeoutColor = "#e0a030";
constexpr const char *kTagBanColor = "#e8554e";

} // namespace unified_chat
