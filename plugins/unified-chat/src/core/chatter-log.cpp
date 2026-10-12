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

#include "chatter-log.hpp"

#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat {

bool ChatterLog::Add(Platform platform, std::string_view id)
{
	if (id.empty())
		return false;
	std::string key = (platform == Platform::Twitch ? "t:" : "y:") + std::string(id);
	if (!ids_.insert(key).second)
		return false;
	order_.push_back(std::move(key));
	if (order_.size() > kMaxChatters) {
		ids_.erase(order_.front());
		order_.pop_front();
	}
	dirty_ = true;
	return !Learning();
}

void ChatterLog::EndSession()
{
	if (!order_.empty()) // a session nobody chatted in taught the list nothing
		++sessions_;
	dirty_ = true;
}

std::string ChatterLog::Serialize()
{
	json obj = {{"version", 1}, {"sessions", sessions_}, {"chatters", order_}};
	dirty_ = false;
	return obj.dump();
}

ChatterLog ChatterLog::Parse(const std::string &text)
{
	ChatterLog log;
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return log;
	if (auto sessions = obj.find("sessions"); sessions != obj.end() && sessions->is_number_integer())
		log.sessions_ = sessions->get<int>();
	if (auto chatters = obj.find("chatters"); chatters != obj.end() && chatters->is_array()) {
		for (const auto &id : *chatters) {
			if (id.is_string() && log.ids_.insert(id.get<std::string>()).second)
				log.order_.push_back(id.get<std::string>());
		}
	}
	return log;
}

} // namespace unified_chat
