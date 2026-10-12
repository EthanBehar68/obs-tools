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

#include "net/socket-wait.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace obs_tools {

// One EventSub WebSocket over curl (CONNECT_ONLY 2). curl answers pings itself. Not thread-safe: one thread owns it.
class EventSubSocket {
public:
	explicit EventSubSocket(const std::atomic<bool> *stop);
	~EventSubSocket();
	EventSubSocket(const EventSubSocket &) = delete;
	EventSubSocket &operator=(const EventSubSocket &) = delete;

	// Connects and upgrades, within kOpenTimeout; on failure error says why.
	bool Open(const std::string &url, std::string &error);

	enum class Read { Message, Again, Closed };
	// One complete text message, Again when nothing more is waiting, Closed when the connection ended. Reads data
	// curl has already decrypted too, so call until Again before Wait.
	Read Next(std::string &message);

	// Sleeps until data arrives, wakeEvent is set, or timeoutMs passes.
	void Wait(void *wakeEvent, int timeoutMs);

private:
	static constexpr auto kOpenTimeout = std::chrono::seconds(20);

	// Read by curl's progress callback while Open runs.
	struct OpenState {
		const std::atomic<bool> *stop;
		std::chrono::steady_clock::time_point deadline;
	};

	static int AbortOpen(void *openState, curl_off_t, curl_off_t, curl_off_t, curl_off_t);

	const std::atomic<bool> *stop_;
	OpenState openState_{};
	void *curl_ = nullptr;
	curl_socket_t sock_ = CURL_SOCKET_BAD;
	std::unique_ptr<SocketEvent> event_;
	std::string partial_;
};

} // namespace obs_tools
