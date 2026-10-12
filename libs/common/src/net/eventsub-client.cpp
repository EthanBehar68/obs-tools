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

#include "eventsub-client.hpp"
#include "eventsub-socket.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <memory>

namespace unified_chat {

using Clock = std::chrono::steady_clock;

EventSubResult RunEventSub(const std::string &url, const std::atomic<bool> &stop, void *wakeEvent,
			   const EventSubHandlers &h)
{
	EventSubResult result;
	auto socket = std::make_unique<EventSubSocket>(&stop);
	if (!socket->Open(url, result.error)) {
		result.end = stop ? EventSubResult::End::Stopped : EventSubResult::End::ConnectFailed;
		return result;
	}

	eventsub::Session session(Clock::now());
	std::unique_ptr<EventSubSocket> successor; // while moving to a new socket at Twitch's request
	const char *name = h.logName.c_str();

	// Reads everything waiting on one socket; false when it closed or the session must end.
	EventSubResult::End ending = EventSubResult::End::Closed;
	auto drain = [&](EventSubSocket &from, bool isSuccessor) {
		std::string text;
		for (;;) {
			auto read = from.Next(text);
			if (read == EventSubSocket::Read::Again)
				return true;
			if (read == EventSubSocket::Read::Closed)
				return false;
			auto step = session.Handle(text, isSuccessor, Clock::now());
			if (!step.subscribe.empty()) {
				if (!h.subscribe(step.subscribe)) {
					ending = EventSubResult::End::SubscribeFailed;
					return false;
				}
				result.subscribed = true;
			}
			if (step.notification && h.notification)
				h.notification(*step.notification);
			if (step.revocation && h.revocation && h.revocation(*step.revocation)) {
				ending = EventSubResult::End::Revoked;
				return false;
			}
			if (!step.reconnectTo.empty()) {
				obs_log(LOG_INFO, "%s: EventSub asked to move to a new connection", name);
				successor = std::make_unique<EventSubSocket>(&stop);
				std::string error;
				if (!successor->Open(step.reconnectTo, error)) {
					obs_log(LOG_WARNING, "%s: EventSub new connection failed: %s", name,
						error.c_str());
					successor.reset(); // the old socket closes soon; then a fresh start
					session.AbandonReconnect();
				}
			}
			if (step.promote)
				return true; // handled by the caller of drain, once this socket is read to the end
		}
	};

	while (!stop) {
		if (h.work && !h.work()) {
			result.end = EventSubResult::End::CallerEnded;
			return result;
		}

		const bool open = drain(*socket, false);
		const bool wasReconnecting = session.Reconnecting();
		if (successor && !drain(*successor, true)) {
			successor.reset();
			session.AbandonReconnect();
		}
		if (h.afterRead)
			h.afterRead();
		if (ending != EventSubResult::End::Closed) {
			result.end = ending;
			return result;
		}
		if (successor && wasReconnecting && !session.Reconnecting()) {
			socket = std::move(successor); // the old one was read to the end above
			obs_log(LOG_INFO, "%s: EventSub moved to the new connection", name);
		} else if (!open) {
			result.end = stop ? EventSubResult::End::Stopped : EventSubResult::End::Closed;
			return result;
		}

		const auto now = Clock::now();
		if (successor && now > session.ReconnectDeadline()) {
			obs_log(LOG_WARNING, "%s: EventSub new connection sent no welcome in time", name);
			successor.reset();
			session.AbandonReconnect();
		}
		if (now > session.Deadline()) {
			result.end = EventSubResult::End::TimedOut;
			return result;
		}

		auto due = session.Deadline() + std::chrono::milliseconds(1);
		if (h.nextDue)
			due = (std::min)(due, h.nextDue());
		auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(due - Clock::now()).count() + 1;
		if (successor)
			waitMs = (std::min)(waitMs, 100LL); // two sockets for a few seconds: check both often
		socket->Wait(wakeEvent, (int)std::clamp<long long>(waitMs, 0, 60 * 60 * 1000));
	}
	result.end = EventSubResult::End::Stopped;
	return result;
}

} // namespace unified_chat
