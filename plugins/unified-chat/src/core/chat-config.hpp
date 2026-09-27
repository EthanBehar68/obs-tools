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
#include "core/oauth-device.hpp"
#include "core/secret-codec.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
	int youtubePollSeconds = 8; // polling interval, also used when streaming falls back
	bool youtubeStream = true;  // server-pushed chat (streamList) instead of polling
	// Connect YouTube chat only while OBS's main output is streaming (no quota used while offline), rather than
	// always looking for a live broadcast.
	bool youtubeConnectOnStream = true;

	SendTarget sendTarget = SendTarget::Both;
	int maxMessages = 500;
	// Accounts whose identical Twitch and YouTube messages are shown as one line.
	std::vector<std::string> mergeBots{"Nightbot"};
};

// "Nightbot, StreamElements" <-> {"Nightbot", "StreamElements"}; empty entries are dropped.
std::vector<std::string> SplitNameList(std::string_view text);
std::string JoinNameList(const std::vector<std::string> &names);

// Tokens and the Google client secret are written as "enc:v1:<base64>" when a codec is given.
std::string SerializeConfig(const ChatConfig &config, const SecretCodec *codec = nullptr);

struct ParseReport {
	bool plaintextSecrets = false;  // at least one secret was stored unencrypted (an older config)
	bool unreadableSecrets = false; // at least one encrypted secret couldn't be decrypted (it's left empty)
};
// Missing or malformed fields fall back to defaults, so a damaged file never blocks OBS startup.
ChatConfig ParseConfig(const std::string &json, const SecretCodec *codec = nullptr, ParseReport *report = nullptr);

} // namespace unified_chat
