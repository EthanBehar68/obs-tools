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
		{"overflow_message", settings.overflowMessage},
		{"variant_message", settings.variantMessage}};
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
	text("variant_message", settings.variantMessage);
}

AlertSettings &AlertsConfig::For(AlertKind kind)
{
	switch (kind) {
	case AlertKind::Follow:
		return follow;
	case AlertKind::TwitchSub:
		return twitchSub;
	case AlertKind::GiftSubs:
		return giftSubs;
	case AlertKind::Cheer:
		return cheer;
	case AlertKind::Raid:
		return raid;
	case AlertKind::Subscriber:
		return subscriber;
	case AlertKind::SuperChat:
		return superChat;
	case AlertKind::Member:
		return member;
	default:
		return giftMemberships;
	}
}

const char *KindKey(AlertKind kind)
{
	switch (kind) {
	case AlertKind::Follow:
		return "follow";
	case AlertKind::TwitchSub:
		return "twitch_sub";
	case AlertKind::GiftSubs:
		return "gift_subs";
	case AlertKind::Cheer:
		return "cheer";
	case AlertKind::Raid:
		return "raid";
	case AlertKind::Subscriber:
		return "subscriber";
	case AlertKind::SuperChat:
		return "super_chat";
	case AlertKind::Member:
		return "member";
	default:
		return "gift_memberships";
	}
}

std::string SerializeAlertsConfig(const AlertsConfig &config)
{
	json obj = {{"duration_seconds", config.durationSeconds},
		    {"min_bits", config.minBits},
		    {"min_raid_viewers", config.minRaidViewers}};
	for (AlertKind kind : kAllKinds)
		obj[KindKey(kind)] = ToJson(config.For(kind));
	return obj.dump(4);
}

AlertsConfig ParseAlertsConfig(const std::string &text)
{
	AlertsConfig config;
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return config;
	for (AlertKind kind : kAllKinds)
		FromJson(obj, KindKey(kind), config.For(kind));
	auto number = [&](const char *key, int &out, int low, int high) {
		auto it = obj.find(key);
		if (it != obj.end() && it->is_number_integer())
			out = std::clamp(it->get<int>(), low, high);
	};
	number("duration_seconds", config.durationSeconds, 1, 60);
	number("min_bits", config.minBits, 1, 1000000);
	number("min_raid_viewers", config.minRaidViewers, 1, 1000000);
	return config;
}

} // namespace stream_alerts
