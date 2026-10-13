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
	} else if constexpr (std::is_same_v<T, bool>) {
		return it->is_boolean() ? it->get<bool>() : fallback;
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

static json HighlightsToJson(const Highlights &highlights)
{
	json obj = json::object();
	for (size_t i = 0; i < kHighlightKinds; ++i) {
		const Highlight &h = highlights.kinds[i];
		obj[HighlightKey((HighlightKind)i)] = {{"show", h.show}, {"color", h.color}, {"icon", h.icon}};
	}
	return obj;
}

// Kinds missing from the file keep their defaults; a bad colour falls back to the default colour.
static void HighlightsFromJson(const json &obj, Highlights &highlights)
{
	for (size_t i = 0; i < kHighlightKinds; ++i) {
		auto it = obj.find(HighlightKey((HighlightKind)i));
		if (it == obj.end() || !it->is_object())
			continue;
		Highlight &h = highlights.kinds[i];
		h.show = Get<bool>(*it, "show", h.show);
		const std::string color = Get<std::string>(*it, "color", h.color);
		if (color.empty() || !SanitizeColor(color).empty())
			h.color = SanitizeColor(color);
		h.icon = SanitizeIcon(Get<std::string>(*it, "icon", h.icon));
	}
}

static json StreamInfoToJson(const ChatConfig &config)
{
	json recent = json::array();
	for (const auto &r : config.recentCategories) {
		recent.push_back({{"id", r.category.id},
				  {"name", r.category.name},
				  {"box_art_url", r.category.boxArtUrl},
				  {"tags", r.tags}});
	}
	json scenes = json::array();
	for (const auto &link : config.sceneCategories) {
		scenes.push_back({{"scene_uuid", link.sceneUuid},
				  {"scene_name", link.sceneName},
				  {"id", link.category.id},
				  {"name", link.category.name},
				  {"box_art_url", link.category.boxArtUrl}});
	}
	return {{"twitch", config.streamInfoTwitch},
		{"youtube", config.streamInfoYouTube},
		{"recent_categories", recent},
		{"category_by_scene", config.categoryByScene},
		{"scene_delay_seconds", config.categorySceneDelaySeconds},
		{"scenes", scenes}};
}

static void StreamInfoFromJson(const json &obj, ChatConfig &config)
{
	config.streamInfoTwitch = Get<bool>(obj, "twitch", config.streamInfoTwitch);
	config.streamInfoYouTube = Get<bool>(obj, "youtube", config.streamInfoYouTube);
	config.categoryByScene = Get<bool>(obj, "category_by_scene", config.categoryByScene);
	config.categorySceneDelaySeconds =
		std::clamp(Get<int>(obj, "scene_delay_seconds", kDefaultSceneCategoryDelay), 0, kMaxSceneCategoryDelay);
	if (auto scenes = obj.find("scenes"); scenes != obj.end() && scenes->is_array()) {
		for (const auto &item : *scenes) {
			if (!item.is_object())
				continue;
			LinkSceneCategory(config.sceneCategories,
					  {Get<std::string>(item, "scene_uuid", {}),
					   Get<std::string>(item, "scene_name", {}),
					   {Get<std::string>(item, "id", {}), Get<std::string>(item, "name", {}),
					    Get<std::string>(item, "box_art_url", {})}});
		}
	}
	auto recent = obj.find("recent_categories");
	if (recent == obj.end() || !recent->is_array())
		return;
	for (auto it = recent->rbegin(); it != recent->rend(); ++it) { // oldest first, so the newest ends up first
		if (!it->is_object())
			continue;
		RecentCategory r;
		r.category = {Get<std::string>(*it, "id", {}), Get<std::string>(*it, "name", {}),
			      Get<std::string>(*it, "box_art_url", {})};
		if (r.category.name.empty())
			continue;
		if (auto tags = it->find("tags"); tags != it->end() && tags->is_array()) {
			// A hand-edited file can't hold tags Twitch would refuse.
			for (const auto &tag : *tags) {
				const std::string normalized = tag.is_string() ? NormalizeTag(tag.get<std::string>())
									       : "";
				if (!normalized.empty() && r.tags.size() < kTwitchMaxTags)
					r.tags.push_back(normalized);
			}
		}
		RememberCategory(config.recentCategories, std::move(r));
	}
}

std::string SerializeConfig(const ChatConfig &config)
{
	json obj = {
		{"twitch",
		 {{"channel", config.twitchChannel}, {"chat_method", config.twitchEventSub ? "eventsub" : "irc"}}},
		{"youtube",
		 {{"video", config.youtubeVideo},
		  {"poll_seconds", config.youtubePollSeconds},
		  {"chat_method", config.youtubeStream ? "stream" : "poll"},
		  {"connect", config.youtubeConnectOnStream ? "on_stream" : "always"}}},
		{"send_target", std::string(SendTargetToString(config.sendTarget))},
		{"max_messages", config.maxMessages},
		{"viewer_check_minutes", config.viewerCheckMinutes},
		{"merge_bots", config.mergeBots},
		{"show_alerts", config.showAlerts},
		{"highlights", HighlightsToJson(config.highlights)},
		{"stream_info", StreamInfoToJson(config)},
		{"display",
		 {{"text_size_percent", config.textSizePercent},
		  {"timestamps", config.timestamps == TimestampStyle::Hours24   ? "24h"
				 : config.timestamps == TimestampStyle::Hours12 ? "12h"
										: "off"},
		  {"compact", config.compact}}},
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
		config.twitchEventSub = Get<std::string>(*twitch, "chat_method", "eventsub") != "irc";
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
	config.viewerCheckMinutes = std::clamp(Get<int>(obj, "viewer_check_minutes", 5), 0, 60);

	if (auto highlights = obj.find("highlights"); highlights != obj.end() && highlights->is_object())
		HighlightsFromJson(*highlights, config.highlights);
	if (auto display = obj.find("display"); display != obj.end() && display->is_object()) {
		const int percent = Get<int>(*display, "text_size_percent", 100);
		config.textSizePercent = std::clamp((percent + 5) / 10 * 10, kMinTextSizePercent, kMaxTextSizePercent);
		const std::string timestamps = Get<std::string>(*display, "timestamps", "off");
		config.timestamps = timestamps == "24h"   ? TimestampStyle::Hours24
				    : timestamps == "12h" ? TimestampStyle::Hours12
							  : TimestampStyle::Off;
		config.compact = Get<bool>(*display, "compact", false);
	}
	if (auto streamInfo = obj.find("stream_info"); streamInfo != obj.end() && streamInfo->is_object())
		StreamInfoFromJson(*streamInfo, config);
	auto showAlerts = obj.find("show_alerts");
	if (showAlerts != obj.end() && showAlerts->is_boolean())
		config.showAlerts = showAlerts->get<bool>();

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
