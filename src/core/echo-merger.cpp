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

#include "echo-merger.hpp"
#include "text-util.hpp"

#include <algorithm>

namespace unified_chat {

uint64_t EchoMerger::Begin(const std::vector<Platform> &platforms, int64_t nowMs)
{
	uint64_t id = nextId_++;
	pending_[id] = {platforms, {}, nowMs + timeoutMs_};
	return id;
}

std::optional<DisplayLine> EchoMerger::Merge(const Pending &pending)
{
	if (pending.echoes.empty())
		return std::nullopt;

	// Always list Twitch before YouTube, regardless of which platform echoed first.
	std::vector<ChatMessage> echoes = pending.echoes;
	std::stable_sort(echoes.begin(), echoes.end(),
			 [](const ChatMessage &a, const ChatMessage &b) { return a.platform < b.platform; });

	DisplayLine line{echoes.front(), {}};
	for (const auto &echo : echoes) {
		line.platforms.push_back(echo.platform);
		if (&echo != &echoes.front() && !echo.author.empty() &&
		    ToLower(echo.author) != ToLower(line.message.author))
			line.message.author += " / " + echo.author;
	}
	return line;
}

std::vector<DisplayLine> EchoMerger::ReleaseIfDone(uint64_t sendId)
{
	auto it = pending_.find(sendId);
	if (it == pending_.end() || !it->second.waiting.empty())
		return {};

	auto line = Merge(it->second);
	pending_.erase(it);
	if (!line)
		return {};
	return {std::move(*line)};
}

std::vector<DisplayLine> EchoMerger::Offer(const ChatMessage &message)
{
	auto it = message.sendId ? pending_.find(message.sendId) : pending_.end();
	if (it == pending_.end())
		return {{message, {message.platform}}};

	auto &waiting = it->second.waiting;
	auto platform = std::find(waiting.begin(), waiting.end(), message.platform);
	if (platform == waiting.end())
		return {{message, {message.platform}}};

	waiting.erase(platform);
	it->second.echoes.push_back(message);
	return ReleaseIfDone(message.sendId);
}

std::vector<DisplayLine> EchoMerger::Fail(uint64_t sendId, Platform platform)
{
	auto it = pending_.find(sendId);
	if (it == pending_.end())
		return {};
	auto &waiting = it->second.waiting;
	waiting.erase(std::remove(waiting.begin(), waiting.end(), platform), waiting.end());
	return ReleaseIfDone(sendId);
}

std::vector<DisplayLine> EchoMerger::Expire(int64_t nowMs)
{
	std::vector<DisplayLine> lines;
	for (auto it = pending_.begin(); it != pending_.end();) {
		if (nowMs < it->second.deadline) {
			++it;
			continue;
		}
		if (auto line = Merge(it->second))
			lines.push_back(std::move(*line));
		it = pending_.erase(it);
	}
	return lines;
}

} // namespace unified_chat
