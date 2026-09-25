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

#include "echo-merger.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unified_chat {

// Folds a bot's identical message on the other platform (e.g. a Nightbot timer posted to Twitch and YouTube)
// into the line already shown for it. Nothing is held back: the first copy is shown at once and the view
// updates that line when the second copy arrives. YouTube copies lag Twitch by the poll interval, hence the
// generous window.
class BotMerger {
public:
	static constexpr int64_t kWindowMs = 30000;
	static constexpr size_t kMaxRecent = 64;

	// Names are matched case-insensitively, ignoring surrounding spaces and a leading '@'.
	void SetBots(const std::vector<std::string> &names);
	bool IsBot(std::string_view author) const;

	struct Merged {
		int lineId;       // the line to update
		DisplayLine line; // that line, now listing both platforms
	};

	// For a single-platform bot line about to be shown: returns the earlier line it duplicates, if any
	// (and forgets it, so a third copy becomes a new line).
	std::optional<Merged> Match(const ChatMessage &message, int64_t nowMs);
	// Records a bot line that was just shown under lineId.
	void Remember(const ChatMessage &message, int lineId, int64_t nowMs);

	size_t RecentCount() const { return recent_.size(); }

private:
	struct Recent {
		std::string key;
		ChatMessage message;
		int lineId;
		int64_t time;
	};

	void Prune(int64_t nowMs);

	std::vector<std::string> bots_; // normalized: lower case, no '@'
	std::deque<Recent> recent_;
};

} // namespace unified_chat
