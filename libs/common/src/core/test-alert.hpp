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

#pragma once

#include <optional>
#include <string>

namespace obs_tools {

// A Stream Alerts test of a kind that normally reaches the chat dock from the chats themselves (subs, gift subs,
// cheers, raids, Super Chats, memberships, Jewels), so the dock shows the test too. Follows and YouTube subscribers
// keep the older obs_tools_alert signal.
struct TestAlert {
	std::string kind; // Stream Alerts' kind key, e.g. "twitch_sub"; the dock's highlight kinds use the same keys
	bool twitch = true;
	std::string text; // the alert's message as shown on screen
};

// {"v":1,"kind":"twitch_sub","platform":"twitch","text":"..."}
std::string SerializeTestAlert(const TestAlert &alert);
// nullopt for anything malformed, or a newer "v" this version can't read.
std::optional<TestAlert> ParseTestAlert(const std::string &json);

} // namespace obs_tools
