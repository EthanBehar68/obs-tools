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

#include "chat-router.hpp"
#include "core/text-util.hpp"
#include "twitch-irc.hpp"
#include "youtube-api.hpp"

namespace unified_chat {

std::string_view SendTargetToString(SendTarget target)
{
	switch (target) {
	case SendTarget::Twitch:
		return "twitch";
	case SendTarget::YouTube:
		return "youtube";
	default:
		return "both";
	}
}

SendTarget SendTargetFromString(std::string_view value)
{
	if (value == "twitch")
		return SendTarget::Twitch;
	if (value == "youtube")
		return SendTarget::YouTube;
	return SendTarget::Both;
}

SendPlan PlanSend(SendTarget target, std::string_view input, bool twitchReady, bool youtubeReady)
{
	SendPlan plan;
	plan.text = Trim(StripLineBreaks(input));
	if (plan.text.empty()) {
		plan.blocked = true;
		return plan;
	}

	struct Candidate {
		Platform platform;
		bool ready;
		size_t limit;
	};
	std::vector<Candidate> wanted;
	if (target != SendTarget::YouTube)
		wanted.push_back({Platform::Twitch, twitchReady, twitch::kMaxMessageLength});
	if (target != SendTarget::Twitch)
		wanted.push_back({Platform::YouTube, youtubeReady, youtube::kMaxMessageLength});

	size_t length = Utf8Length(plan.text);
	for (const auto &candidate : wanted) {
		if (length > candidate.limit) {
			plan.blocked = true;
			plan.errors.push_back(std::string(PlatformName(candidate.platform)) + ": message is " +
					      std::to_string(length) + " characters, the limit is " +
					      std::to_string(candidate.limit));
		}
	}
	if (plan.blocked)
		return plan;

	for (const auto &candidate : wanted) {
		if (candidate.ready)
			plan.targets.push_back(candidate.platform);
		else
			plan.errors.push_back(std::string(PlatformName(candidate.platform)) +
					      ": not connected, message not sent there");
	}
	plan.blocked = plan.targets.empty();
	return plan;
}

} // namespace unified_chat
