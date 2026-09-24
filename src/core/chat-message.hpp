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

namespace unified_chat {

enum class Platform { Twitch, YouTube };

struct ChatMessage {
	Platform platform = Platform::Twitch;
	std::string id;
	std::string author;
	std::string text;
	std::string color; // "#RRGGBB" or empty
	bool isAction = false;
	bool isSelf = false;
	uint64_t sendId = 0; // set on local echoes of a message sent from the dock
};

inline std::string_view PlatformName(Platform platform)
{
	return platform == Platform::Twitch ? "Twitch" : "YouTube";
}

} // namespace unified_chat
