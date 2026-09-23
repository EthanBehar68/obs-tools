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

namespace unified_chat {

// Image resource names registered on the chat view's QTextDocument.
constexpr const char *kTwitchIconResource = "unified-chat://icon/twitch";
constexpr const char *kYouTubeIconResource = "unified-chat://icon/youtube";

std::string_view DefaultNameColor(Platform platform);
std::string_view IconResource(Platform platform);

// Renders one chat line as Qt rich text: platform icon, display name, message.
// All user-controlled text is escaped.
std::string FormatMessageHtml(const ChatMessage &message, int iconSize);
std::string FormatNoticeHtml(std::string_view text);

} // namespace unified_chat
