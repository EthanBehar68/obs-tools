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

#include "chat-message.hpp"

#include <deque>
#include <string>
#include <string_view>
#include <unordered_set>

namespace unified_chat {

// Who has chatted in your channels, saved between sessions, to spot first-time chatters where the platform doesn't
// say (YouTube never does; Twitch only for its introduce-yourself prompt over EventSub). During the list's first
// session it only learns: otherwise everyone would look new on the first stream.
class ChatterLog {
public:
	static constexpr size_t kMaxChatters = 50000; // about 1.5 MB; the oldest are forgotten first

	// True when this chatter wasn't known before (and the list is past its first session).
	bool Add(Platform platform, std::string_view id);
	bool Learning() const { return sessions_ == 0; }
	// A session ended (OBS closing): the list now knows that session's chatters.
	void EndSession();
	bool Dirty() const { return dirty_; }
	size_t Size() const { return order_.size(); }

	std::string Serialize();
	static ChatterLog Parse(const std::string &json);

private:
	std::unordered_set<std::string> ids_; // "t:<user id>", "y:<channel id>"
	std::deque<std::string> order_;       // oldest first
	int sessions_ = 0;
	bool dirty_ = false;
};

} // namespace unified_chat
