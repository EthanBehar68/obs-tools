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
	if (IsPriority(event.kind)) {
		if (!event.id.empty() && !seen_.insert(std::to_string((int)event.kind) + ":" + event.id).second)
			return false; // the same event again
		// Ahead of the first waiting follow or subscriber, behind other priority alerts.
		auto at = std::find_if(queue_.begin(), queue_.end(),
				       [](const Alert &alert) { return !IsPriority(alert.kind); });
		queue_.insert(at, {event.kind, {event.name}, false, event});
		return true;
	}

	const std::string key = std::to_string((int)event.kind) + ":" + event.id;
	if (!seen_.insert(key).second)
		return false;

	auto combined = std::find_if(queue_.begin(), queue_.end(),
				     [&](const Alert &alert) { return alert.combined && alert.kind == event.kind; });
	if (combined != queue_.end()) {
		combined->names.push_back(event.name);
	} else if (individual_ < kMaxQueued) {
		queue_.push_back({event.kind, {event.name}, false, event});
		++individual_;
	} else {
		queue_.push_back({event.kind, {event.name}, true, {}});
	}
	return true;
}

std::optional<Alert> AlertQueue::Pop()
{
	if (queue_.empty())
		return std::nullopt;
	Alert alert = std::move(queue_.front());
	queue_.pop_front();
	if (!alert.combined && !IsPriority(alert.kind))
		--individual_;
	return alert;
}

size_t AlertQueue::RemoveIf(const std::function<bool(const Alert &)> &match)
{
	size_t removed = 0;
	for (auto it = queue_.begin(); it != queue_.end();) {
		if (it->combined || !match(*it)) {
			++it;
			continue;
		}
		if (!IsPriority(it->kind))
			--individual_;
		it = queue_.erase(it);
		++removed;
	}
	return removed;
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
	if (!alert.combined) {
		const AlertEvent &event = alert.event;
		std::string text =
			ReplaceAll(single, "{name}", alert.names.empty() ? std::string() : alert.names.front());
		text = ReplaceAll(text, "{months}", std::to_string(event.months));
		text = ReplaceAll(text, "{count}", std::to_string(event.count));
		text = ReplaceAll(text, "{viewers}", std::to_string(event.viewers));
		text = ReplaceAll(text, "{amount}", event.amount);
		text = ReplaceAll(text, "{level}", event.level);
		return ReplaceAll(text, "{message}", event.message); // last: the viewer's text is never expanded
	}

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
