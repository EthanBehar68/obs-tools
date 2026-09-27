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
	bool Refresh(unified_chat::Service service);

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

// Twitch follows of the signed-in account's channel: EventSub channel.follow over a WebSocket. The thread sleeps
// until Twitch sends something (a keepalive at least once a minute).
class FollowListener : public Listener {
public:
	explicit FollowListener(ListenerCallbacks callbacks);
	~FollowListener() override;

private:
	enum class Outcome { Ok, Retry, Reconnect, Stop };
	void Run() override;
	Outcome Authorize(std::string &userId);
	Outcome Session(const std::string &url, bool resumed, const std::string &userId, std::string &reconnectUrl);
	Outcome Subscribe(const std::string &sessionId, const std::string &userId);
};

// New YouTube subscribers: the recent-subscribers feed every kSubscriberCheckSeconds, compared with the last one.
class SubscriberPoller : public Listener {
public:
	explicit SubscriberPoller(ListenerCallbacks callbacks);
	~SubscriberPoller() override;

private:
	void Run() override;
};

} // namespace stream_alerts
