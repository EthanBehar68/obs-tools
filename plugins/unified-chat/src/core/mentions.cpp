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

#include "mentions.hpp"
#include "core/text-util.hpp"

#include <algorithm>
#include <cctype>

namespace unified_chat {

static constexpr std::string_view kMentionPrefix = "unified-chat://mention/";

static char Lower(char c)
{
	return (char)std::tolower((unsigned char)c);
}

static bool IsNameChar(char c)
{
	// Twitch logins and YouTube handles: letters, digits, '_', '-', '.'; non-ASCII bytes count as letters.
	return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || (unsigned char)c >= 0x80;
}

static bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix)
{
	if (text.size() < prefix.size())
		return false;
	for (size_t i = 0; i < prefix.size(); ++i) {
		if (Lower(text[i]) != Lower(prefix[i]))
			return false;
	}
	return true;
}

static std::string_view WithoutAt(std::string_view name)
{
	return !name.empty() && name.front() == '@' ? name.substr(1) : name;
}

static std::string UrlDecode(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '%' && i + 2 < text.size() && std::isxdigit((unsigned char)text[i + 1]) &&
		    std::isxdigit((unsigned char)text[i + 2])) {
			out.push_back((char)std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
			i += 2;
		} else {
			out.push_back(text[i]);
		}
	}
	return out;
}

std::string BuildMentionLink(Platform platform, std::string_view mention, std::string_view messageId)
{
	return std::string(kMentionPrefix) + (platform == Platform::Twitch ? "twitch/" : "youtube/") +
	       UrlEncode(mention) + "/" + UrlEncode(messageId);
}

std::optional<MentionLink> ParseMentionLink(std::string_view url)
{
	if (url.rfind(kMentionPrefix, 0) != 0)
		return std::nullopt;
	url.remove_prefix(kMentionPrefix.size());

	MentionLink link;
	if (url.rfind("twitch/", 0) == 0) {
		link.platform = Platform::Twitch;
		url.remove_prefix(7);
	} else if (url.rfind("youtube/", 0) == 0) {
		link.platform = Platform::YouTube;
		url.remove_prefix(8);
	} else {
		return std::nullopt;
	}

	auto slash = url.find('/');
	link.mention = UrlDecode(url.substr(0, slash));
	if (slash != std::string_view::npos)
		link.messageId = UrlDecode(url.substr(slash + 1));
	if (link.mention.empty())
		return std::nullopt;
	return link;
}

void MentionMatcher::SetNames(const std::vector<std::string> &names)
{
	names_.clear();
	for (const auto &name : names) {
		std::string bare = ToLower(Trim(WithoutAt(name)));
		if (!bare.empty() && std::find(names_.begin(), names_.end(), bare) == names_.end())
			names_.push_back(std::move(bare));
	}
}

bool MentionMatcher::Matches(std::string_view text) const
{
	for (const auto &name : names_) {
		for (size_t pos = 0; pos + name.size() <= text.size(); ++pos) {
			if (pos > 0 && IsNameChar(text[pos - 1]))
				continue;
			if (!StartsWithIgnoreCase(text.substr(pos), name))
				continue;
			size_t end = pos + name.size();
			if (end == text.size() || !IsNameChar(text[end]))
				return true;
		}
	}
	return false;
}

void RecentChatters::Add(Platform platform, const std::string &mention)
{
	if (mention.empty())
		return;
	std::string key = (platform == Platform::Twitch ? "t:" : "y:") + ToLower(mention);
	if (auto it = index_.find(key); it != index_.end()) {
		order_.splice(order_.begin(), order_, it->second);
		it->second->mention = mention; // keep the latest spelling
		return;
	}
	order_.push_front({platform, mention});
	index_.emplace(std::move(key), order_.begin());
	if (order_.size() > kMaxEntries) {
		const Entry &oldest = order_.back();
		index_.erase((oldest.platform == Platform::Twitch ? "t:" : "y:") + ToLower(oldest.mention));
		order_.pop_back();
	}
}

std::vector<RecentChatters::Entry> RecentChatters::Complete(std::string_view prefix, std::optional<Platform> platform,
							    size_t limit) const
{
	prefix = WithoutAt(prefix);
	std::vector<Entry> matches;
	for (const auto &entry : order_) {
		if (matches.size() >= limit)
			break;
		if (platform && entry.platform != *platform)
			continue;
		if (StartsWithIgnoreCase(entry.mention, prefix))
			matches.push_back(entry);
	}
	return matches;
}

} // namespace unified_chat
