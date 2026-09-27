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

#pragma once

#include "core/oauth-device.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace unified_chat {

// Runs one OAuth device authorization on a worker thread. Callbacks run on that thread.
class DeviceLogin {
public:
	struct Callbacks {
		std::function<void(const oauth::DeviceCode &)> onCode;
		// On success error is empty. login is filled for Twitch only.
		std::function<void(const oauth::Token &, const std::string &login, const std::string &error)> onFinished;
	};

	DeviceLogin(oauth::Provider provider, Callbacks callbacks);
	~DeviceLogin();

	DeviceLogin(const DeviceLogin &) = delete;
	DeviceLogin &operator=(const DeviceLogin &) = delete;

private:
	void Run();
	void WaitFor(int seconds);

	oauth::Provider provider_;
	Callbacks callbacks_;
	std::atomic<bool> stop_ = false;
	std::mutex mutex_;
	std::condition_variable cv_;
	std::thread thread_;
};

} // namespace unified_chat
