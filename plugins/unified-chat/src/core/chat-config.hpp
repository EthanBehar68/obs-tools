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
#include "core/accounts.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unified_chat {

struct ChatConfig {
	// Sign-ins and client IDs are shared by all plugins (core/accounts.hpp), not stored here.
	std::string twitchChannel;

	std::string youtubeVideo;
	int youtubePollSeconds = 8; // polling interval, also used when streaming falls back
	bool youtubeStream = true;  // server-pushed chat (streamList) instead of polling
	// Connect YouTube chat only while OBS's main output is streaming (no quota used while offline), rather than
	// always looking for a live broadcast.
	bool youtubeConnectOnStream = true;

	SendTarget sendTarget = SendTarget::Both;
	int maxMessages = 500;
	// Accounts whose identical Twitch and YouTube messages are shown as one line.
	std::vector<std::string> mergeBots{"Nightbot"};
	// New followers and subscribers from the Stream Alerts plugin, as lines in the chat.
	bool showAlerts = true;
};

// "Nightbot, StreamElements" <-> {"Nightbot", "StreamElements"}; empty entries are dropped.
std::vector<std::string> SplitNameList(std::string_view text);
std::string JoinNameList(const std::vector<std::string> &names);

std::string SerializeConfig(const ChatConfig &config);

// Missing or malformed fields fall back to defaults, so a damaged file never blocks OBS startup.
// Configs from before the shared accounts (1.2.0 and older) also hold sign-ins: those go to legacy, decrypted
// with codec.
ChatConfig ParseConfig(const std::string &json, Accounts *legacy = nullptr, const SecretCodec *codec = nullptr,
		       SecretReport *report = nullptr);

} // namespace unified_chat
