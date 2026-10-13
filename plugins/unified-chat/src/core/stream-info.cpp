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

#include "stream-info.hpp"
#include "moderation.hpp"
#include "core/text-util.hpp"

#include <algorithm>
#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat {

static constexpr const char *kHelix = "https://api.twitch.tv/helix";
static constexpr const char *kYouTubeApi = "https://www.googleapis.com/youtube/v3";

static std::string String(const json &obj, const char *key)
{
	auto it = obj.find(key);
	return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// The first object of a Helix "data" array, or of a Data API "items" array.
static const json *First(const json &obj, const char *array)
{
	if (!obj.is_object())
		return nullptr;
	auto it = obj.find(array);
	if (it == obj.end() || !it->is_array() || it->empty() || !(*it)[0].is_object())
		return nullptr;
	return &(*it)[0];
}

// ---- Twitch

std::string TwitchChannelUrl(const std::string &broadcasterId)
{
	return std::string(kHelix) + "/channels?broadcaster_id=" + UrlEncode(broadcasterId);
}

std::optional<TwitchChannelInfo> ParseTwitchChannelInfo(const std::string &text)
{
	const json obj = json::parse(text, nullptr, false);
	const json *channel = First(obj, "data");
	if (!channel)
		return std::nullopt;
	TwitchChannelInfo info;
	info.title = String(*channel, "title");
	info.category.id = String(*channel, "game_id");
	info.category.name = String(*channel, "game_name");
	if (auto tags = channel->find("tags"); tags != channel->end() && tags->is_array()) {
		for (const auto &tag : *tags) {
			if (tag.is_string())
				info.tags.push_back(tag.get<std::string>());
		}
	}
	return info;
}

std::string TwitchCategorySearchUrl(std::string_view query)
{
	return std::string(kHelix) + "/search/categories?first=10&query=" + UrlEncode(Trim(query));
}

std::vector<TwitchCategory> ParseTwitchCategories(const std::string &text)
{
	std::vector<TwitchCategory> categories;
	const json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return categories;
	auto data = obj.find("data");
	if (data == obj.end() || !data->is_array())
		return categories;
	for (const auto &item : *data) {
		if (!item.is_object())
			continue;
		TwitchCategory category{String(item, "id"), String(item, "name"), String(item, "box_art_url")};
		if (!category.id.empty() && !category.name.empty())
			categories.push_back(std::move(category));
	}
	return categories;
}

std::string BoxArtUrl(const TwitchCategory &category, int width, int height)
{
	const std::string size = std::to_string(width) + "x" + std::to_string(height);
	std::string url = category.boxArtUrl;
	if (url.empty()) {
		if (category.id.empty())
			return {};
		url = "https://static-cdn.jtvnw.net/ttv-boxart/" + UrlEncode(category.id) + "-{width}x{height}.jpg";
	}
	if (auto at = url.find("{width}x{height}"); at != std::string::npos)
		return url.replace(at, 16, size);
	// "...-52x72.jpg": the size is the part between the last '-' and the extension.
	const size_t dash = url.rfind('-');
	const size_t dot = url.rfind('.');
	if (dash == std::string::npos || dot == std::string::npos || dot < dash)
		return url;
	const std::string current = url.substr(dash + 1, dot - dash - 1);
	const size_t x = current.find('x');
	if (x == std::string::npos || x == 0 || x + 1 == current.size() ||
	    !std::all_of(current.begin(), current.end(), [](char c) { return c == 'x' || (c >= '0' && c <= '9'); }))
		return url;
	return url.replace(dash + 1, current.size(), size);
}

std::string BuildTwitchChannelPatch(const TwitchChannelUpdate &update)
{
	json body = json::object();
	if (update.title)
		body["title"] = *update.title;
	if (update.categoryId)
		body["game_id"] = update.categoryId->empty() ? "0" : *update.categoryId; // "0" clears it
	if (update.tags)
		body["tags"] = *update.tags; // [] removes every tag
	return body.dump();
}

static bool IsAsciiAlnum(unsigned char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string NormalizeTag(std::string_view tag)
{
	std::string text = Trim(tag);
	if (!text.empty() && text.front() == '#')
		text.erase(0, 1);
	if (text.empty() || Utf8Length(text) > kTwitchTagMax)
		return {};
	// Bytes from 0x80 up are other alphabets' letters (Twitch takes those); ASCII must be a letter or digit.
	for (unsigned char c : text) {
		if (c < 0x80 && !IsAsciiAlnum(c))
			return {};
	}
	return text;
}

std::vector<std::string> ParseTags(std::string_view text, std::vector<std::string> *rejected)
{
	std::vector<std::string> tags;
	std::vector<std::string> seen; // lower case
	size_t at = 0;
	while (at < text.size()) {
		const size_t end = text.find_first_of(", \t\r\n", at);
		const std::string_view word = text.substr(at, end == std::string_view::npos ? text.npos : end - at);
		at = end == std::string_view::npos ? text.size() : end + 1;
		if (Trim(word).empty())
			continue;
		const std::string tag = NormalizeTag(word);
		if (tag.empty()) {
			if (rejected)
				rejected->emplace_back(Trim(word));
			continue;
		}
		const std::string key = ToLower(tag);
		if (std::find(seen.begin(), seen.end(), key) != seen.end() || tags.size() >= kTwitchMaxTags)
			continue;
		seen.push_back(key);
		tags.push_back(tag);
	}
	return tags;
}

std::string JoinTags(const std::vector<std::string> &tags)
{
	std::string text;
	for (const auto &tag : tags)
		text += (text.empty() ? "" : ", ") + tag;
	return text;
}

void RememberCategory(std::vector<RecentCategory> &recent, RecentCategory used)
{
	if (used.category.id.empty())
		return;
	recent.erase(std::remove_if(recent.begin(), recent.end(),
				    [&](const RecentCategory &r) { return r.category.id == used.category.id; }),
		     recent.end());
	recent.insert(recent.begin(), std::move(used));
	if (recent.size() > kMaxRecentCategories)
		recent.resize(kMaxRecentCategories);
}

// ---- YouTube

std::string YouTubeBroadcastsUrl(bool active)
{
	return std::string(kYouTubeApi) +
	       "/liveBroadcasts?part=snippet&broadcastStatus=" + (active ? "active" : "upcoming") +
	       "&broadcastType=all&maxResults=10&fields=items(id,snippet(title,scheduledStartTime))";
}

std::optional<YouTubeBroadcast> PickYouTubeBroadcast(const std::string &text, bool active)
{
	const json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;
	auto items = obj.find("items");
	if (items == obj.end() || !items->is_array())
		return std::nullopt;
	std::optional<YouTubeBroadcast> best;
	int64_t bestStart = 0;
	for (const auto &item : *items) {
		if (!item.is_object() || String(item, "id").empty())
			continue;
		const json snippet = item.value("snippet", json::object());
		const int64_t start = snippet.is_object() ? ParseRfc3339(String(snippet, "scheduledStartTime")) : 0;
		// Unscheduled ones (0) come after scheduled ones.
		const bool sooner = !best || (start > 0 && (bestStart == 0 || start < bestStart));
		if (!sooner)
			continue;
		best = YouTubeBroadcast{String(item, "id"), snippet.is_object() ? String(snippet, "title") : "",
					active};
		bestStart = start;
		if (active)
			break; // there is only ever one live
	}
	return best;
}

std::string YouTubeVideoUrl(const std::string &videoId)
{
	// Exactly the snippet's writable fields: what's read is what videos.update gets back.
	return std::string(kYouTubeApi) + "/videos?part=snippet&id=" + UrlEncode(videoId) +
	       "&fields=items/snippet(title,description,tags,categoryId,defaultLanguage,defaultAudioLanguage)";
}

std::optional<YouTubeVideoSnippet> ParseYouTubeVideoSnippet(const std::string &text)
{
	const json obj = json::parse(text, nullptr, false);
	const json *item = First(obj, "items");
	if (!item)
		return std::nullopt;
	auto snippet = item->find("snippet");
	if (snippet == item->end() || !snippet->is_object())
		return std::nullopt;
	return YouTubeVideoSnippet{String(*snippet, "title"), String(*snippet, "categoryId"), snippet->dump()};
}

std::string YouTubeVideoUpdateUrl()
{
	return std::string(kYouTubeApi) + "/videos?part=snippet&fields=snippet(title,categoryId)";
}

std::optional<std::string> BuildYouTubeVideoUpdate(const std::string &videoId, const YouTubeVideoSnippet &current,
						   const std::optional<std::string> &title,
						   const std::optional<std::string> &categoryId)
{
	json snippet = json::parse(current.json, nullptr, false);
	if (!snippet.is_object() || videoId.empty())
		return std::nullopt;
	if (title)
		snippet["title"] = *title;
	if (categoryId)
		snippet["categoryId"] = *categoryId;
	// Both are required by videos.update.
	if (String(snippet, "title").empty() || String(snippet, "categoryId").empty())
		return std::nullopt;
	return json{{"id", videoId}, {"snippet", snippet}}.dump();
}

bool YouTubeTitleAllowed(std::string_view title)
{
	return title.find_first_of("<>") == std::string_view::npos;
}

const std::vector<YouTubeCategory> kYouTubeCategories = {
	{"1", "Film & Animation"},
	{"2", "Autos & Vehicles"},
	{"10", "Music"},
	{"15", "Pets & Animals"},
	{"17", "Sports"},
	{"19", "Travel & Events"},
	{"20", "Gaming"},
	{"22", "People & Blogs"},
	{"23", "Comedy"},
	{"24", "Entertainment"},
	{"25", "News & Politics"},
	{"26", "Howto & Style"},
	{"27", "Education"},
	{"28", "Science & Technology"},
	{"29", "Nonprofits & Activism"},
};

std::string StreamInfoErrorMessage(const std::string &json)
{
	return ModerationErrorMessage(json); // the same Twitch and Google error shapes
}

} // namespace unified_chat
