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

#include "emotes.hpp"

#include <algorithm>
#include <cstdlib>
#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat {

// ---- Twitch tag ------------------------------------------------------------------------------------------

std::vector<EmoteRange> ParseTwitchEmotes(std::string_view tag)
{
	std::vector<EmoteRange> ranges;
	while (!tag.empty()) {
		auto slash = tag.find('/');
		std::string_view entry = tag.substr(0, slash);
		tag = slash == std::string_view::npos ? std::string_view() : tag.substr(slash + 1);

		auto colon = entry.find(':');
		if (colon == std::string_view::npos || colon == 0)
			continue;
		const std::string id(entry.substr(0, colon));
		std::string_view list = entry.substr(colon + 1);
		while (!list.empty()) {
			auto comma = list.find(',');
			std::string_view pair = list.substr(0, comma);
			list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
			auto dash = pair.find('-');
			if (dash == std::string_view::npos)
				continue;
			const std::string first(pair.substr(0, dash)), last(pair.substr(dash + 1));
			char *endFirst = nullptr, *endLast = nullptr;
			const long start = std::strtol(first.c_str(), &endFirst, 10);
			const long end = std::strtol(last.c_str(), &endLast, 10);
			if (first.empty() || last.empty() || *endFirst || *endLast || start < 0 || end < start)
				continue;
			ranges.push_back({id, (int)start, (int)end});
		}
	}
	std::sort(ranges.begin(), ranges.end(),
		  [](const EmoteRange &a, const EmoteRange &b) { return a.start < b.start; });
	return ranges;
}

// ---- Third-party lists -------------------------------------------------------------------------------------

static std::string IdString(const json &value)
{
	if (value.is_string())
		return value.get<std::string>();
	if (value.is_number_integer())
		return std::to_string(value.get<long long>());
	return {};
}

static double Aspect(const json &obj, const char *widthKey = "width", const char *heightKey = "height")
{
	auto w = obj.find(widthKey), h = obj.find(heightKey);
	if (w == obj.end() || h == obj.end() || !w->is_number() || !h->is_number())
		return 1.0;
	const double width = w->get<double>(), height = h->get<double>();
	return width > 0 && height > 0 ? std::clamp(width / height, 0.25, 4.0) : 1.0;
}

static void AddBttv(const json &list, std::vector<Emote> &out)
{
	if (!list.is_array())
		return;
	for (const auto &item : list) {
		if (!item.is_object())
			continue;
		Emote emote{EmoteProvider::BTTV, IdString(item.value("id", json())), item.value("code", std::string()),
			    Aspect(item)};
		emote.animated = item.value("animated", false) || item.value("imageType", std::string()) == "gif";
		if (!emote.id.empty() && !emote.name.empty())
			out.push_back(std::move(emote));
	}
}

std::vector<Emote> ParseBttvEmotes(const std::string &body)
{
	std::vector<Emote> emotes;
	json obj = json::parse(body, nullptr, false);
	if (obj.is_array()) { // global list
		AddBttv(obj, emotes);
	} else if (obj.is_object()) { // a channel: its own and shared emotes
		AddBttv(obj.value("channelEmotes", json()), emotes);
		AddBttv(obj.value("sharedEmotes", json()), emotes);
	}
	return emotes;
}

static void AddFfzSet(const json &set, std::vector<Emote> &out)
{
	auto list = set.find("emoticons");
	if (list == set.end() || !list->is_array())
		return;
	for (const auto &item : *list) {
		if (!item.is_object())
			continue;
		Emote emote{EmoteProvider::FFZ, IdString(item.value("id", json())), item.value("name", std::string()),
			    Aspect(item)};
		if (!emote.id.empty() && !emote.name.empty())
			out.push_back(std::move(emote));
	}
}

std::vector<Emote> ParseFfzEmotes(const std::string &body)
{
	std::vector<Emote> emotes;
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return emotes;
	auto sets = obj.find("sets");
	if (sets == obj.end() || !sets->is_object())
		return emotes;

	// Which sets apply: the room's set, or the global default sets; otherwise every set in the response.
	std::vector<std::string> wanted;
	if (auto room = obj.find("room"); room != obj.end() && room->is_object() && room->contains("set"))
		wanted.push_back(IdString((*room)["set"]));
	if (auto defaults = obj.find("default_sets"); defaults != obj.end() && defaults->is_array())
		for (const auto &id : *defaults)
			wanted.push_back(IdString(id));

	for (auto it = sets->begin(); it != sets->end(); ++it) {
		if (wanted.empty() || std::find(wanted.begin(), wanted.end(), it.key()) != wanted.end())
			AddFfzSet(it.value(), emotes);
	}
	return emotes;
}

std::vector<Emote> ParseSevenTvEmotes(const std::string &body)
{
	std::vector<Emote> emotes;
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return emotes;
	// A user response nests the set; the global set is the object itself.
	const json *set = &obj;
	if (auto nested = obj.find("emote_set"); nested != obj.end() && nested->is_object())
		set = &*nested;
	auto list = set->find("emotes");
	if (list == set->end() || !list->is_array())
		return emotes;

	for (const auto &item : *list) {
		if (!item.is_object())
			continue;
		Emote emote{EmoteProvider::SevenTV, IdString(item.value("id", json())),
			    item.value("name", std::string())};
		// The first listed file (1x) gives the shape.
		if (auto data = item.find("data"); data != item.end() && data->is_object()) {
			emote.animated = data->value("animated", false);
			if (auto host = data->find("host"); host != data->end() && host->is_object()) {
				if (auto files = host->find("files"); files != host->end() && files->is_array() &&
								      !files->empty() && (*files)[0].is_object())
					emote.aspect = Aspect((*files)[0]);
			}
		}
		if (!emote.id.empty() && !emote.name.empty())
			emotes.push_back(std::move(emote));
	}
	return emotes;
}

// ---- Index and splitting ---------------------------------------------------------------------------------

void EmoteIndex::Add(const std::vector<Emote> &emotes, int priority)
{
	for (const auto &emote : emotes) {
		auto it = byName_.find(emote.name);
		if (it == byName_.end())
			byName_.emplace(emote.name, Entry{emote, priority});
		else if (priority >= it->second.priority)
			it->second = Entry{emote, priority};
	}
}

const Emote *EmoteIndex::Find(std::string_view word) const
{
	if (byName_.empty())
		return nullptr;
	auto it = byName_.find(std::string(word));
	return it == byName_.end() ? nullptr : &it->second.emote;
}

int EmotePriority(EmoteProvider provider, bool channel)
{
	int service = provider == EmoteProvider::SevenTV ? 3 : provider == EmoteProvider::BTTV ? 2 : 1;
	return (channel ? 10 : 0) + service;
}

static void AppendText(std::vector<MessageSegment> &segments, std::string_view text)
{
	if (text.empty())
		return;
	if (!segments.empty() && !segments.back().emote)
		segments.back().text.append(text);
	else
		segments.push_back({std::string(text), std::nullopt});
}

// Words of a text piece that name an emote become emote segments; the rest (spaces included) stays text.
static void SplitWords(std::vector<MessageSegment> &segments, std::string_view text, const EmoteIndex *index)
{
	if (!index || index->Size() == 0) {
		AppendText(segments, text);
		return;
	}
	size_t pos = 0;
	while (pos < text.size()) {
		size_t space = text.find(' ', pos);
		size_t end = space == std::string_view::npos ? text.size() : space;
		std::string_view word = text.substr(pos, end - pos);
		if (const Emote *emote = word.empty() ? nullptr : index->Find(word))
			segments.push_back({{}, *emote});
		else
			AppendText(segments, word);
		if (space == std::string_view::npos)
			break;
		AppendText(segments, " ");
		pos = space + 1;
	}
}

std::vector<MessageSegment> SplitMessage(std::string_view text, const std::vector<EmoteRange> &twitchEmotes,
					 const EmoteIndex *index)
{
	// Byte offset where each code point starts (plus the end), since Twitch counts characters.
	std::vector<size_t> starts;
	for (size_t i = 0; i < text.size(); ++i) {
		if (((unsigned char)text[i] & 0xC0) != 0x80)
			starts.push_back(i);
	}
	starts.push_back(text.size());
	const int characters = (int)starts.size() - 1;

	// Ranges must be ordered, non-overlapping and inside the text; otherwise don't trust any of them.
	bool usable = true;
	int previousEnd = -1;
	for (const auto &range : twitchEmotes) {
		if (range.start <= previousEnd || range.end >= characters) {
			usable = false;
			break;
		}
		previousEnd = range.end;
	}

	std::vector<MessageSegment> segments;
	size_t cursor = 0;
	if (usable) {
		for (const auto &range : twitchEmotes) {
			const size_t from = starts[(size_t)range.start], to = starts[(size_t)range.end + 1];
			SplitWords(segments, text.substr(cursor, from - cursor), index);
			segments.push_back(
				{{},
				 Emote{EmoteProvider::Twitch, range.id, std::string(text.substr(from, to - from))}});
			cursor = to;
		}
	}
	SplitWords(segments, text.substr(cursor), index);
	return segments;
}

// ---- Images ------------------------------------------------------------------------------------------------

static const char *ProviderTag(EmoteProvider provider)
{
	switch (provider) {
	case EmoteProvider::BTTV:
		return "b";
	case EmoteProvider::FFZ:
		return "f";
	case EmoteProvider::SevenTV:
		return "s";
	default:
		return "t";
	}
}

std::string EmoteImageKey(const Emote &emote)
{
	return std::string("unified-chat://emote/") + ProviderTag(emote.provider) + "/" + emote.id;
}

std::string EmoteImageUrl(const Emote &emote, bool hiDpi)
{
	// Static images only: animated ones would redraw the chat on every frame. For an animated BTTV or 7TV emote
	// the service's static file is used (one frame, ~2 KB instead of a ~300 KB animation); those static files
	// exist only for animated emotes, so still ones use the normal address. FFZ's plain URLs are always static.
	const std::string scale = hiDpi ? "2x" : "1x";
	switch (emote.provider) {
	case EmoteProvider::BTTV:
		return "https://cdn.betterttv.net/emote/" + emote.id +
		       (emote.animated ? "/static/" + scale + ".webp" : "/" + scale);
	case EmoteProvider::FFZ:
		return "https://cdn.frankerfacez.com/emote/" + emote.id + (hiDpi ? "/2" : "/1");
	case EmoteProvider::SevenTV:
		return "https://cdn.7tv.app/emote/" + emote.id + "/" + scale +
		       (emote.animated ? "_static.webp" : ".webp");
	default:
		return "https://static-cdn.jtvnw.net/emoticons/v2/" + emote.id + "/static/dark/" +
		       (hiDpi ? "2.0" : "1.0");
	}
}

// ---- Badges ------------------------------------------------------------------------------------------------

std::vector<Badge> ParseRoleBadges(std::string_view tag)
{
	static const char *const kOrder[] = {"broadcaster", "moderator", "vip", "founder", "subscriber"};
	std::vector<Badge> all;
	while (!tag.empty()) {
		auto comma = tag.find(',');
		std::string_view entry = tag.substr(0, comma);
		tag = comma == std::string_view::npos ? std::string_view() : tag.substr(comma + 1);
		auto slash = entry.find('/');
		if (slash != std::string_view::npos && slash > 0)
			all.push_back({std::string(entry.substr(0, slash)), std::string(entry.substr(slash + 1))});
	}
	std::vector<Badge> roles;
	for (const char *set : kOrder) {
		for (const auto &badge : all) {
			if (badge.set == set)
				roles.push_back(badge);
		}
	}
	// Twitch shows a founder badge instead of a subscriber one.
	if (std::any_of(roles.begin(), roles.end(), [](const Badge &b) { return b.set == "founder"; }))
		roles.erase(std::remove_if(roles.begin(), roles.end(),
					   [](const Badge &b) { return b.set == "subscriber"; }),
			    roles.end());
	return roles;
}

std::unordered_map<std::string, std::string> ParseBadgeImages(const std::string &body, bool hiDpi)
{
	std::unordered_map<std::string, std::string> images;
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object() || !obj.contains("data") || !obj["data"].is_array())
		return images;
	for (const auto &set : obj["data"]) {
		if (!set.is_object() || !set.contains("versions") || !set["versions"].is_array())
			continue;
		const std::string setId = set.value("set_id", std::string());
		for (const auto &version : set["versions"]) {
			if (!version.is_object())
				continue;
			std::string url = version.value(hiDpi ? "image_url_2x" : "image_url_1x", std::string());
			if (url.empty())
				url = version.value("image_url_1x", std::string());
			const std::string id = IdString(version.value("id", json()));
			if (!setId.empty() && !id.empty() && !url.empty())
				images[setId + "/" + id] = url;
		}
	}
	return images;
}

std::string BadgeKey(const Badge &badge)
{
	return badge.set + "/" + badge.version;
}

std::string BadgeImageKey(const Badge &badge)
{
	return "unified-chat://badge/" + BadgeKey(badge);
}

} // namespace unified_chat
