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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace unified_chat {

enum class SendTarget { Twitch, YouTube, Both };

std::string_view SendTargetToString(SendTarget target);
SendTarget SendTargetFromString(std::string_view value);

struct SendPlan {
	std::string text;
	std::vector<Platform> targets;
	std::vector<std::string> errors;
	// True when the message must not be sent anywhere (e.g. too long), so the input should be kept.
	bool blocked = false;
};

// Decides where a message goes. Platforms that are not connected are skipped with an error,
// while a message that is too long for any selected platform blocks the whole send.
SendPlan PlanSend(SendTarget target, std::string_view input, bool twitchReady, bool youtubeReady);

inline SendTarget TargetFor(Platform platform)
{
	return platform == Platform::Twitch ? SendTarget::Twitch : SendTarget::YouTube;
}

// Mentioning someone switches the send target to their platform only for that message: the target the user
// chose ("home") is remembered at the first switch and restored after sending.
class MentionTarget {
public:
	// The target for mentioning someone on platform. current is the target shown right now.
	SendTarget Switch(SendTarget current, Platform platform)
	{
		if (!home_)
			home_ = current;
		return TargetFor(platform);
	}
	// The target the user chose: home while a mention switch is pending, otherwise current.
	SendTarget Home(SendTarget current) const { return home_ ? *home_ : current; }
	bool Pending() const { return home_.has_value(); }
	// After sending (or dropping the mention): the target to go back to, if a switch was pending.
	std::optional<SendTarget> Restore()
	{
		auto home = home_;
		home_.reset();
		return home;
	}
	// The user picked a target themselves; it becomes home and nothing is restored.
	void Forget() { home_.reset(); }

private:
	std::optional<SendTarget> home_;
};

} // namespace unified_chat
