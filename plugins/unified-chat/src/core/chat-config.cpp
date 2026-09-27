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

#include "chat-config.hpp"
#include "core/text-util.hpp"

#include <algorithm>
#include <type_traits>
#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat {

template<typename T> static T Get(const json &obj, const char *key, T fallback)
{
	auto it = obj.find(key);
	if (it == obj.end())
		return fallback;
	if constexpr (std::is_same_v<T, std::string>) {
		return it->is_string() ? it->get<std::string>() : fallback;
	} else {
		return it->is_number_integer() ? it->get<T>() : fallback;
	}
}

static oauth::Token TokenFromJson(const json &obj, const char *key, const SecretCodec *codec, SecretReport *report)
{
	oauth::Token token;
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_object())
		return token;
	token.accessToken = OpenSecret(Get<std::string>(*it, "access_token", {}), codec, report);
	token.refreshToken = OpenSecret(Get<std::string>(*it, "refresh_token", {}), codec, report);
	token.expiresAt = Get<int64_t>(*it, "expires_at", 0);
	return token;
}

std::vector<std::string> SplitNameList(std::string_view text)
{
	std::vector<std::string> names;
	while (!text.empty()) {
		auto comma = text.find(',');
		std::string name = Trim(text.substr(0, comma));
		if (!name.empty())
			names.push_back(std::move(name));
		text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
	}
	return names;
}

std::string JoinNameList(const std::vector<std::string> &names)
{
	std::string text;
	for (const auto &name : names) {
		if (!text.empty())
			text += ", ";
		text += name;
	}
	return text;
}

std::string SerializeConfig(const ChatConfig &config)
{
	json obj = {
		{"twitch", {{"channel", config.twitchChannel}}},
		{"youtube",
		 {{"video", config.youtubeVideo},
		  {"poll_seconds", config.youtubePollSeconds},
		  {"chat_method", config.youtubeStream ? "stream" : "poll"},
		  {"connect", config.youtubeConnectOnStream ? "on_stream" : "always"}}},
		{"send_target", std::string(SendTargetToString(config.sendTarget))},
		{"max_messages", config.maxMessages},
		{"merge_bots", config.mergeBots},
	};
	return obj.dump(4);
}

ChatConfig ParseConfig(const std::string &text, Accounts *legacy, const SecretCodec *codec, SecretReport *report)
{
	ChatConfig config;
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return config;

	auto twitch = obj.find("twitch");
	if (twitch != obj.end() && twitch->is_object()) {
		config.twitchChannel = Get<std::string>(*twitch, "channel", {});
		if (legacy) {
			legacy->twitch.clientId = Get<std::string>(*twitch, "client_id", {});
			legacy->twitch.login = Get<std::string>(*twitch, "login", {});
			legacy->twitch.token = TokenFromJson(*twitch, "token", codec, report);
		}
	}

	auto youtube = obj.find("youtube");
	if (youtube != obj.end() && youtube->is_object()) {
		config.youtubeVideo = Get<std::string>(*youtube, "video", {});
		config.youtubePollSeconds = std::clamp(Get<int>(*youtube, "poll_seconds", 8), 1, 120);
		config.youtubeStream = Get<std::string>(*youtube, "chat_method", "stream") != "poll";
		config.youtubeConnectOnStream = Get<std::string>(*youtube, "connect", "on_stream") != "always";
		if (legacy) {
			legacy->google.clientId = Get<std::string>(*youtube, "client_id", {});
			legacy->google.clientSecret =
				OpenSecret(Get<std::string>(*youtube, "client_secret", {}), codec, report);
			legacy->google.token = TokenFromJson(*youtube, "token", codec, report);
		}
	}

	config.sendTarget = SendTargetFromString(Get<std::string>(obj, "send_target", "both"));
	config.maxMessages = std::clamp(Get<int>(obj, "max_messages", 500), 50, 10000);

	// Missing: keep the default. Present: use it as saved, even when the user emptied it.
	auto bots = obj.find("merge_bots");
	if (bots != obj.end() && bots->is_array()) {
		config.mergeBots.clear();
		for (const auto &bot : *bots) {
			if (bot.is_string() && !bot.get<std::string>().empty())
				config.mergeBots.push_back(bot.get<std::string>());
		}
	}
	return config;
}

} // namespace unified_chat
