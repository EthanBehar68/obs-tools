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

#include "test-alert.hpp"

#include <json.hpp>

using json = nlohmann::json;

namespace obs_tools {

static constexpr int kVersion = 1;

std::string SerializeTestAlert(const TestAlert &alert)
{
	return json{{"v", kVersion},
		    {"kind", alert.kind},
		    {"platform", alert.twitch ? "twitch" : "youtube"},
		    {"text", alert.text}}
		.dump();
}

std::optional<TestAlert> ParseTestAlert(const std::string &text)
{
	const json obj = json::parse(text, nullptr, false);
	if (!obj.is_object() || obj.value("v", 0) != kVersion)
		return std::nullopt;
	auto kind = obj.find("kind");
	auto message = obj.find("text");
	if (kind == obj.end() || !kind->is_string() || message == obj.end() || !message->is_string())
		return std::nullopt;
	return TestAlert{kind->get<std::string>(), obj.value("platform", "") != "youtube", message->get<std::string>()};
}

} // namespace obs_tools
