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

#include "core/accounts.hpp"
#include "core/alert-queue.hpp"
#include "core/eventsub.hpp"
#include "core/platform-events.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace stream_alerts {

// Callbacks run on the listener's thread.
struct ListenerCallbacks {
	std::function<void(AlertEvent)> onEvent;
	std::function<void(const std::string &status)> onStatus; // only when it changes
};

// Base for a worker thread that is stopped and joined on destruction; waits wake up at once for that.
class Listener {
public:
	virtual ~Listener();
	Listener(const Listener &) = delete;
	Listener &operator=(const Listener &) = delete;

protected:
	explicit Listener(ListenerCallbacks callbacks);
	void Start();
	void Stop();
	virtual void Run() = 0;
	void WaitFor(int ms);
	void Status(const std::string &status);
	// Through the shared store, so a single-use Twitch refresh token is never spent twice.
	// False when no new token was had; refreshRejected_ then says whether the sign-in was refused (sign out) or
	// the refresh only failed for now (network, server: retry later).
	bool Refresh(unified_chat::Service service);
	bool refreshRejected_ = false;

	ListenerCallbacks callbacks_;
	unified_chat::Account account_;
	std::atomic<bool> stop_ = false;
	void *wakeEvent_ = nullptr;

private:
	std::string status_;
	std::mutex mutex_;
	std::condition_variable cv_;
	std::thread thread_;
};

// Twitch events in the signed-in account's channel over one EventSub WebSocket: follows (channel.follow), subs,
// gift subs and raids (channel.chat.notification), cheers (channel.cheer), as wanted. The thread sleeps until
// Twitch sends something (a keepalive at least once a minute).
class TwitchListener : public Listener {
public:
	TwitchListener(ListenerCallbacks callbacks, TwitchWants wants);
	~TwitchListener() override;

private:
	enum class Outcome { Ok, Retry, Stop };
	void Run() override;
	Outcome Authorize(std::string &userId);
	Outcome Session(const std::string &userId);
	Outcome Subscribe(const std::string &sessionId, const std::string &userId);
	void Handle(const unified_chat::eventsub::Message &message);

	TwitchWants wants_;   // what the user turned on
	TwitchWants allowed_; // of that, what the sign-in allows
	std::string missing_; // for the status: what a new sign-in would add
};

// New YouTube subscribers: the recent-subscribers feed every intervalSeconds (1 quota unit each), compared with the
// last one.
class SubscriberPoller : public Listener {
public:
	SubscriberPoller(ListenerCallbacks callbacks, int intervalSeconds);
	~SubscriberPoller() override;

private:
	void Run() override;

	int intervalSeconds_;
};

} // namespace stream_alerts
