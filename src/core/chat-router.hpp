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

enum class SendTarget { Twitch, YouTube, Both };

std::string_view SendTargetToString(SendTarget target);
SendTarget SendTargetFromString(std::string_view value);

struct SendPlan {
	std::string text;
	std::vector<Platform> targets;
	std::vector<std::string> errors;
	// True when the message must not be sent anywhere (e.g. too long), so the input should be kept.
	bool blocked = false;
};

// Decides where a message goes. Platforms that are not connected are skipped with an error,
// while a message that is too long for any selected platform blocks the whole send.
SendPlan PlanSend(SendTarget target, std::string_view input, bool twitchReady, bool youtubeReady);

} // namespace unified_chat
