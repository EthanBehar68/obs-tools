/*
obs-stream-alerts
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

#include "alerts-config.hpp"

#include <algorithm>
#include <json.hpp>

using json = nlohmann::json;

namespace stream_alerts {

static json ToJson(const AlertSettings &settings)
{
	return {{"enabled", settings.enabled},
		{"source", settings.source},
		{"text_source", settings.textSource},
		{"message", settings.message},
		{"overflow_message", settings.overflowMessage}};
}

static void FromJson(const json &obj, const char *key, AlertSettings &settings)
{
	auto it = obj.find(key);
	if (it == obj.end() || !it->is_object())
		return;
	auto text = [&](const char *field, std::string &out) {
		auto value = it->find(field);
		if (value != it->end() && value->is_string())
			out = value->get<std::string>();
	};
	auto enabled = it->find("enabled");
	if (enabled != it->end() && enabled->is_boolean())
		settings.enabled = enabled->get<bool>();
	text("source", settings.source);
	text("text_source", settings.textSource);
	text("message", settings.message);
	text("overflow_message", settings.overflowMessage);
}

std::string SerializeAlertsConfig(const AlertsConfig &config)
{
	json obj = {{"follow", ToJson(config.follow)},
		    {"subscriber", ToJson(config.subscriber)},
		    {"duration_seconds", config.durationSeconds}};
	return obj.dump(4);
}

AlertsConfig ParseAlertsConfig(const std::string &text)
{
	AlertsConfig config;
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return config;
	FromJson(obj, "follow", config.follow);
	FromJson(obj, "subscriber", config.subscriber);
	auto duration = obj.find("duration_seconds");
	if (duration != obj.end() && duration->is_number_integer())
		config.durationSeconds = std::clamp(duration->get<int>(), 1, 60);
	return config;
}

} // namespace stream_alerts
