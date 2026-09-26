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

#include "asset-loader.hpp"
#include "curl-http-client.hpp"

namespace unified_chat {

AssetLoader::AssetLoader()
{
	thread_ = std::thread(&AssetLoader::Run, this);
}

AssetLoader::~AssetLoader()
{
	stop_ = true; // also aborts a transfer in progress (CurlHttpClient's cancel flag)
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

void AssetLoader::Get(std::string url, std::vector<std::string> headers, Done done)
{
	{
		std::lock_guard lock(mutex_);
		jobs_.push_back({std::move(url), std::move(headers), std::move(done)});
	}
	cv_.notify_one();
}

void AssetLoader::Run()
{
	CurlHttpClient http(&stop_);
	while (!stop_) {
		Job job;
		{
			std::unique_lock lock(mutex_);
			cv_.wait(lock, [this] { return stop_.load() || !jobs_.empty(); });
			if (stop_)
				break;
			job = std::move(jobs_.front());
			jobs_.pop_front();
		}
		HttpResponse res = http.Get(job.url, job.headers);
		if (!stop_ && job.done)
			job.done(res);
	}
}

} // namespace unified_chat
