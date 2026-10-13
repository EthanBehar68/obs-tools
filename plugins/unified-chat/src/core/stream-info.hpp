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

#include "obs-tools.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Stream info: the title, Twitch category and tags, and YouTube category, set from OBS.
namespace unified_chat {

// Modify Channel Information needs it; the signed-in user can only change their own channel.
constexpr const char *kStreamInfoScope = "channel:manage:broadcast";
constexpr size_t kTwitchTitleMax = 140; // characters
constexpr size_t kYouTubeTitleMax = 100;
constexpr size_t kTwitchMaxTags = 10;
constexpr size_t kTwitchTagMax = 25;
constexpr size_t kMaxRecentCategories = 10;
// Quota units: videos.list and liveBroadcasts.list cost 1, videos.update 50.
constexpr int kYouTubeUpdateCost = 50;

// ---- Twitch

struct TwitchCategory {
	std::string id; // empty: no category
	std::string name;
	std::string boxArtUrl; // as Twitch gives it; see BoxArtUrl
};

struct TwitchChannelInfo {
	std::string title;
	TwitchCategory category;
	std::vector<std::string> tags;
};

// Get Channel Information, and Modify Channel Information (PATCH) on the same URL.
std::string TwitchChannelUrl(const std::string &broadcasterId);
std::optional<TwitchChannelInfo> ParseTwitchChannelInfo(const std::string &json);

// Search Categories: the first 10 matches.
std::string TwitchCategorySearchUrl(std::string_view query);
std::vector<TwitchCategory> ParseTwitchCategories(const std::string &json);

// Box art at the given size. Search gives "...-52x72.jpg", Get Games "...-{width}x{height}.jpg"; Get Channel
// Information gives none, so without a URL it's built from the category id.
std::string BoxArtUrl(const TwitchCategory &category, int width, int height);

// What Apply changes; a field left empty isn't sent, so Twitch keeps it.
struct TwitchChannelUpdate {
	std::optional<std::string> title;
	std::optional<std::string> categoryId; // "" clears the category
	std::optional<std::vector<std::string>> tags;
};
std::string BuildTwitchChannelPatch(const TwitchChannelUpdate &update);

// A tag as Twitch takes it: a leading '#' dropped, up to 25 characters, letters and digits only. Empty when it
// can't be one.
std::string NormalizeTag(std::string_view tag);
// "English, speedrun #Chill" -> {"English", "speedrun", "Chill"}: split on commas and spaces, each normalized,
// duplicates (any case) and anything past 10 dropped. rejected: the words that can't be tags.
std::vector<std::string> ParseTags(std::string_view text, std::vector<std::string> *rejected = nullptr);
std::string JoinTags(const std::vector<std::string> &tags);

// Recently used categories, newest first, each with the tags last used with it.
struct RecentCategory {
	TwitchCategory category;
	std::vector<std::string> tags;
};
void RememberCategory(std::vector<RecentCategory> &recent, RecentCategory used);

// ---- YouTube

struct YouTubeBroadcast {
	std::string id; // also the video's id
	std::string title;
	bool live = false; // active, else upcoming
};

// Your active broadcasts, or your upcoming ones (1 unit).
std::string YouTubeBroadcastsUrl(bool active);
// The live broadcast, or the upcoming one scheduled soonest.
std::optional<YouTubeBroadcast> PickYouTubeBroadcast(const std::string &json, bool active);

// The video's snippet (1 unit). videos.update replaces the whole snippet and clears any writable field left out,
// so the current one is read first and sent back with only the title and category changed.
std::string YouTubeVideoUrl(const std::string &videoId);
struct YouTubeVideoSnippet {
	std::string title;
	std::string categoryId;
	std::string json; // the snippet object as YouTube sent it
};
std::optional<YouTubeVideoSnippet> ParseYouTubeVideoSnippet(const std::string &json);
// videos.update?part=snippet (PUT, 50 units). nullopt when the snippet can't be read back.
std::string YouTubeVideoUpdateUrl();
std::optional<std::string> BuildYouTubeVideoUpdate(const std::string &videoId, const YouTubeVideoSnippet &current,
						   const std::optional<std::string> &title,
						   const std::optional<std::string> &categoryId);

// YouTube refuses titles with '<' or '>'.
bool YouTubeTitleAllowed(std::string_view title);

// The categories a video can have (videoCategories.list, assignable ones), so the window needs no request.
struct YouTubeCategory {
	const char *id;
	const char *name;
};
extern const std::vector<YouTubeCategory> kYouTubeCategories;

// A short reason from a Twitch or Google error body, or empty.
std::string StreamInfoErrorMessage(const std::string &json);

} // namespace unified_chat
