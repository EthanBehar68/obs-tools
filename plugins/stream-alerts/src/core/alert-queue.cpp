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

#include "alert-queue.hpp"

#include <algorithm>

namespace stream_alerts {

bool AlertQueue::Push(const AlertEvent &event)
{
	const std::string key = (event.kind == AlertKind::Follow ? "f:" : "s:") + event.id;
	if (!seen_.insert(key).second)
		return false;

	auto combined = std::find_if(queue_.begin(), queue_.end(),
				     [&](const Alert &alert) { return alert.combined && alert.kind == event.kind; });
	if (combined != queue_.end()) {
		combined->names.push_back(event.name);
	} else if (individual_ < kMaxQueued) {
		queue_.push_back({event.kind, {event.name}, false});
		++individual_;
	} else {
		queue_.push_back({event.kind, {event.name}, true});
	}
	return true;
}

std::optional<Alert> AlertQueue::Pop()
{
	if (queue_.empty())
		return std::nullopt;
	Alert alert = std::move(queue_.front());
	queue_.pop_front();
	if (!alert.combined)
		--individual_;
	return alert;
}

void AlertQueue::Reset()
{
	queue_.clear();
	individual_ = 0;
	seen_.clear();
}

static std::string ReplaceAll(std::string text, const std::string &from, const std::string &to)
{
	for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
		text.replace(at, from.size(), to);
	return text;
}

std::string FormatAlert(const Alert &alert, const std::string &single, const std::string &combined,
			size_t maxNamesLength)
{
	if (!alert.combined)
		return ReplaceAll(single, "{name}", alert.names.empty() ? std::string() : alert.names.front());

	std::string text = ReplaceAll(combined, "{count}", std::to_string(alert.names.size()));
	std::string names;
	for (size_t i = 0; i < alert.names.size(); ++i) {
		std::string next = (names.empty() ? "" : ", ") + alert.names[i];
		if (names.size() + next.size() > maxNamesLength) {
			names += names.empty() ? "…" : ", …";
			break;
		}
		names += next;
	}
	return names.empty() ? text : text + "\n" + names;
}

} // namespace stream_alerts
