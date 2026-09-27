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

#include "bot-merger.hpp"
#include "core/text-util.hpp"

#include <cctype>
#include <iterator>

namespace unified_chat {

static std::string_view TrimView(std::string_view text)
{
	while (!text.empty() && std::isspace((unsigned char)text.front()))
		text.remove_prefix(1);
	while (!text.empty() && std::isspace((unsigned char)text.back()))
		text.remove_suffix(1);
	return text;
}

static std::string_view BareName(std::string_view name)
{
	name = TrimView(name);
	if (!name.empty() && name.front() == '@')
		name.remove_prefix(1);
	return name;
}

static bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
			return false;
	}
	return true;
}

static std::string Key(const ChatMessage &message)
{
	return ToLower(BareName(message.author)) + '\n' + std::string(TrimView(message.text));
}

void BotMerger::SetBots(const std::vector<std::string> &names)
{
	bots_.clear();
	for (const auto &name : names) {
		std::string bare = ToLower(BareName(name));
		if (!bare.empty())
			bots_.push_back(std::move(bare));
	}
	recent_.clear();
}

bool BotMerger::IsBot(std::string_view author) const
{
	// Runs for every message, so it compares in place instead of building a lower-case copy.
	if (bots_.empty())
		return false;
	std::string_view bare = BareName(author);
	for (const auto &bot : bots_) {
		if (EqualsIgnoreCase(bare, bot))
			return true;
	}
	return false;
}

void BotMerger::Prune(int64_t nowMs)
{
	while (!recent_.empty() && nowMs - recent_.front().time > kWindowMs)
		recent_.pop_front();
}

std::optional<BotMerger::Merged> BotMerger::Match(const ChatMessage &message, int64_t nowMs)
{
	Prune(nowMs);
	if (recent_.empty())
		return std::nullopt;

	const std::string key = Key(message);
	for (auto it = recent_.rbegin(); it != recent_.rend(); ++it) {
		if (it->message.platform == message.platform || it->key != key)
			continue;
		// Twitch first, and the Twitch copy's name and color, as for your own merged lines.
		const ChatMessage &shown = message.platform == Platform::Twitch ? message : it->message;
		Merged merged{it->lineId, {shown, {Platform::Twitch, Platform::YouTube}}};
		recent_.erase(std::next(it).base());
		return merged;
	}
	return std::nullopt;
}

void BotMerger::Remember(const ChatMessage &message, int lineId, int64_t nowMs)
{
	Prune(nowMs);
	if (recent_.size() >= kMaxRecent)
		recent_.pop_front();
	recent_.push_back({Key(message), message, lineId, nowMs});
}

} // namespace unified_chat
