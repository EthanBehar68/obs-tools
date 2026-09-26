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

#include "core/http-client.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace unified_chat {

// Downloads emote lists, badge lists and images one after another on its own thread, over one reused
// connection per host. The thread sleeps while the queue is empty.
class AssetLoader {
public:
	using Done = std::function<void(const HttpResponse &)>; // runs on the loader thread

	AssetLoader();
	~AssetLoader();

	AssetLoader(const AssetLoader &) = delete;
	AssetLoader &operator=(const AssetLoader &) = delete;

	void Get(std::string url, std::vector<std::string> headers, Done done);

private:
	struct Job {
		std::string url;
		std::vector<std::string> headers;
		Done done;
	};

	void Run();

	std::atomic<bool> stop_ = false;
	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<Job> jobs_;
	std::thread thread_;
};

} // namespace unified_chat
