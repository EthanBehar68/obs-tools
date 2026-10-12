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

#include "core/eventsub.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>

namespace unified_chat {

// What the caller does with one EventSub session. All of it runs on the calling thread.
struct EventSubHandlers {
	// Welcomed: subscribe on sessionId (Twitch closes the socket after 10 s without a subscription). False ends
	// the session.
	std::function<bool(const std::string &sessionId)> subscribe;
	std::function<void(const eventsub::Message &)> notification;
	// A subscription was revoked. True ends the session.
	std::function<bool(const eventsub::Message &)> revocation;
	// Each round, before reading: the caller's own work (sends, checks). False ends the session.
	std::function<bool()> work;
	// Each round, after reading everything that was waiting (e.g. to hand a batch on).
	std::function<void()> afterRead;
	// When the caller next needs a round for its own work; the thread sleeps until then at the latest.
	std::function<std::chrono::steady_clock::time_point()> nextDue;
	std::string logName; // e.g. "Twitch chat", for the log lines about moving to a new socket
};

struct EventSubResult {
	enum class End { Stopped, ConnectFailed, SubscribeFailed, Closed, TimedOut, Revoked, CallerEnded };
	End end = End::Stopped;
	bool subscribed = false; // the session got as far as subscribing
	std::string error;       // ConnectFailed
};

// Runs one EventSub session at url until it ends: connects, subscribes on the welcome, delivers notifications
// (duplicates dropped), follows Twitch's moves to a new socket, and notices a dead connection. The thread sleeps
// until data arrives, wakeEvent is set, or a deadline is due.
EventSubResult RunEventSub(const std::string &url, const std::atomic<bool> &stop, void *wakeEvent,
			   const EventSubHandlers &handlers);

} // namespace unified_chat
