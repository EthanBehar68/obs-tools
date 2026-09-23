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

#include "chat-router.hpp"
#include "oauth-device.hpp"

#include <string>

namespace unified_chat {

struct ChatConfig {
	std::string twitchChannel;
	std::string twitchClientId;
	std::string twitchLogin;
	oauth::Token twitchToken;

	std::string youtubeClientId;
	std::string youtubeClientSecret;
	std::string youtubeVideo;
	oauth::Token youtubeToken;
	int youtubePollSeconds = 8;

	SendTarget sendTarget = SendTarget::Both;
	int maxMessages = 500;
};

std::string SerializeConfig(const ChatConfig &config);
// Missing or malformed fields fall back to defaults, so a damaged file never blocks OBS startup.
ChatConfig ParseConfig(const std::string &json);

} // namespace unified_chat
