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

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace unified_chat {

struct DisplayLine {
	ChatMessage message;
	std::vector<Platform> platforms; // icons to show, in order
};

// Collapses the local echoes of one message sent to several platforms into a single line.
// A line is released once every platform has either echoed or failed, or after the timeout.
class EchoMerger {
public:
	explicit EchoMerger(int64_t timeoutMs = 5000) : timeoutMs_(timeoutMs) {}

	// Registers an outgoing message and returns the sendId the connections should tag their echoes with.
	uint64_t Begin(const std::vector<Platform> &platforms, int64_t nowMs);

	// Messages that aren't pending echoes pass straight through as a single-platform line.
	std::vector<DisplayLine> Offer(const ChatMessage &message);
	std::vector<DisplayLine> Fail(uint64_t sendId, Platform platform);
	std::vector<DisplayLine> Expire(int64_t nowMs);

	bool HasPending() const { return !pending_.empty(); }

private:
	struct Pending {
		std::vector<Platform> waiting;
		std::vector<ChatMessage> echoes;
		int64_t deadline = 0;
	};

	static std::optional<DisplayLine> Merge(const Pending &pending);
	std::vector<DisplayLine> ReleaseIfDone(uint64_t sendId);

	int64_t timeoutMs_;
	uint64_t nextId_ = 1;
	std::map<uint64_t, Pending> pending_;
};

} // namespace unified_chat
