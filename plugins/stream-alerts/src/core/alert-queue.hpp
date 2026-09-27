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

#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace stream_alerts {

enum class AlertKind { Follow, Subscriber };

struct AlertEvent {
	AlertKind kind = AlertKind::Follow;
	std::string id;   // the platform's user / channel id: one alert per person per stream
	std::string name; // display name
};

struct Alert {
	AlertKind kind = AlertKind::Follow;
	std::vector<std::string> names; // one, or everyone a combined alert covers
	bool combined = false;
};

// Alerts wait here and play one at a time. Up to kMaxQueued individual alerts wait; past that, a kind's further
// events join one combined alert at the end ("+14 more new followers!"), so a follow-bot flood or a raid doesn't
// take over the screen for half an hour, and nobody is dropped.
class AlertQueue {
public:
	static constexpr size_t kMaxQueued = 10;

	// False when this person already alerted this stream (an unfollow and refollow, a repeated poll).
	bool Push(const AlertEvent &event);
	std::optional<Alert> Pop();
	bool Empty() const { return queue_.empty(); }
	size_t Size() const { return queue_.size(); }
	// A new stream: waiting alerts are dropped and everyone may alert again.
	void Reset();

private:
	std::deque<Alert> queue_;
	size_t individual_ = 0;
	std::unordered_set<std::string> seen_;
};

// single: "{name} just followed!"; combined: "+{count} more new followers!", followed on a second line by as many
// names as fit in maxNamesLength characters.
std::string FormatAlert(const Alert &alert, const std::string &single, const std::string &combined,
			size_t maxNamesLength = 80);

} // namespace stream_alerts
