/*
obs-tools
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

#include "youtube-events.hpp"

#include <algorithm>
#include <iterator>
#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat {

static constexpr const char *kKindNames[] = {"super_chat", "super_sticker", "new_member", "member_milestone",
					     "gift_memberships"};

std::string SerializeYouTubeEvent(const YouTubeEvent &event)
{
	json obj = {{"v", 1}, // the payload's own version, inside kObsToolsProtocol
		    {"kind", kKindNames[(int)event.kind]},
		    {"id", event.id},
		    {"channel_id", event.channelId},
		    {"name", event.name},
		    {"amount", event.amount},
		    {"message", event.message},
		    {"level", event.level},
		    {"months", event.months},
		    {"count", event.count}};
	return obj.dump();
}

std::optional<YouTubeEvent> ParseYouTubeEvent(const std::string &text)
{
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;
	auto string = [&](const char *key) {
		auto it = obj.find(key);
		return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
	};
	auto number = [&](const char *key) {
		auto it = obj.find(key);
		return it != obj.end() && it->is_number_integer() ? it->get<int64_t>() : int64_t(0);
	};

	YouTubeEvent event;
	const std::string kind = string("kind");
	auto found = std::find(std::begin(kKindNames), std::end(kKindNames), kind);
	if (found == std::end(kKindNames))
		return std::nullopt;
	event.kind = (YouTubeEvent::Kind)(found - std::begin(kKindNames));
	event.id = string("id");
	event.channelId = string("channel_id");
	event.name = string("name");
	event.amount = string("amount");
	event.message = string("message");
	event.level = string("level");
	event.months = number("months");
	event.count = number("count");
	return event;
}

} // namespace unified_chat
