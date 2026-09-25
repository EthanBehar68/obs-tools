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

namespace unified_chat {

// Blocking HTTPS client on top of the libcurl that ships with OBS. Requests abort promptly
// once the shared cancel flag is set, so worker threads can be joined quickly on shutdown.
// One curl handle is kept for the client's lifetime so connections, TLS sessions and DNS results are
// reused between requests. Use a client from one thread at a time.
class CurlHttpClient : public HttpClient {
public:
	explicit CurlHttpClient(const std::atomic<bool> *cancel = nullptr) : cancel_(cancel) {}
	~CurlHttpClient() override;

	CurlHttpClient(const CurlHttpClient &) = delete;
	CurlHttpClient &operator=(const CurlHttpClient &) = delete;

	HttpResponse Get(const std::string &url, const std::vector<std::string> &headers) override;
	HttpResponse Post(const std::string &url, const std::vector<std::string> &headers, const std::string &body,
			  const std::string &contentType) override;

private:
	HttpResponse Perform(const std::string &url, const std::vector<std::string> &headers, const std::string *body,
			     const std::string &contentType);

	const std::atomic<bool> *cancel_;
	void *curl_ = nullptr; // CURL *
};

} // namespace unified_chat
