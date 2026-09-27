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

#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace unified_chat {

// A clicked name in the chat view: who to mention, and (Twitch) which message a reply would answer.
struct MentionLink {
	Platform platform = Platform::Twitch;
	std::string mention;   // without '@'
	std::string messageId; // may be empty
};

// "unified-chat://mention/<twitch|youtube>/<mention>/<message id>", URL-encoded parts.
std::string BuildMentionLink(Platform platform, std::string_view mention, std::string_view messageId);
std::optional<MentionLink> ParseMentionLink(std::string_view url);

// Finds whole-word mentions of your own names ("@name" or "name"), ignoring ASCII case. Runs on every message,
// so it compares in place without allocating.
class MentionMatcher {
public:
	void SetNames(const std::vector<std::string> &names); // a leading '@' is ignored
	bool Matches(std::string_view text) const;
	bool Empty() const { return names_.empty(); }

private:
	std::vector<std::string> names_;
};

// Recently seen chatters for "@" + Tab completion, most recent first, per platform.
class RecentChatters {
public:
	static constexpr size_t kMaxEntries = 300;

	struct Entry {
		Platform platform;
		std::string mention;
	};

	void Add(Platform platform, const std::string &mention);
	// Chatters whose mention starts with prefix (ASCII case-insensitive), most recent first. With a platform,
	// only that platform's chatters.
	std::vector<Entry> Complete(std::string_view prefix, std::optional<Platform> platform = std::nullopt,
				    size_t limit = 20) const;
	size_t Size() const { return order_.size(); }

private:
	std::list<Entry> order_; // front = most recent
	std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

} // namespace unified_chat
