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

#include "eventsub.hpp"

using json = nlohmann::json;

namespace unified_chat::eventsub {

std::string StringAt(const json &obj, std::initializer_list<const char *> path)
{
	const json *node = &obj;
	for (const char *key : path) {
		if (!node->is_object())
			return {};
		auto it = node->find(key);
		if (it == node->end())
			return {};
		node = &*it;
	}
	return node->is_string() ? node->get<std::string>() : std::string();
}

std::optional<Message> ParseMessage(const std::string &text)
{
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;

	Message message;
	const std::string type = StringAt(obj, {"metadata", "message_type"});
	if (type.empty())
		return std::nullopt;
	auto payload = obj.find("payload");
	const bool hasPayload = payload != obj.end() && payload->is_object();
	if (type == "session_welcome") {
		message.type = Message::Type::Welcome;
		message.sessionId = StringAt(obj, {"payload", "session", "id"});
		if (hasPayload && payload->contains("session") && (*payload)["session"].is_object()) {
			const json &session = (*payload)["session"];
			auto keepalive = session.find("keepalive_timeout_seconds");
			if (keepalive != session.end() && keepalive->is_number_integer())
				message.keepaliveSeconds = keepalive->get<int>();
		}
		if (message.sessionId.empty())
			return std::nullopt;
	} else if (type == "session_keepalive") {
		message.type = Message::Type::Keepalive;
	} else if (type == "session_reconnect") {
		message.type = Message::Type::Reconnect;
		message.reconnectUrl = StringAt(obj, {"payload", "session", "reconnect_url"});
	} else if (type == "notification") {
		message.type = Message::Type::Notification;
		message.subscriptionType = StringAt(obj, {"metadata", "subscription_type"});
		if (hasPayload) {
			auto event = payload->find("event");
			if (event != payload->end() && event->is_object())
				message.event = std::move(*event);
		}
	} else if (type == "revocation") {
		message.type = Message::Type::Revocation;
		message.subscriptionType = StringAt(obj, {"metadata", "subscription_type"});
		message.status = StringAt(obj, {"payload", "subscription", "status"});
	}
	return message;
}

std::string BuildSubscription(std::string_view type, std::string_view version,
			      const std::vector<std::pair<std::string, std::string>> &condition,
			      const std::string &sessionId)
{
	json conditions = json::object();
	for (const auto &[key, value] : condition)
		conditions[key] = value;
	json body = {{"type", type},
		     {"version", version},
		     {"condition", std::move(conditions)},
		     {"transport", {{"method", "websocket"}, {"session_id", sessionId}}}};
	return body.dump();
}

} // namespace unified_chat::eventsub
