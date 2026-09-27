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

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace unified_chat {

struct HttpResponse {
	long status = 0; // 0 = transport failure, see error
	std::string body;
	std::string error;
	bool interrupted = false; // GetStream stopped early because interrupt() or onData asked it to

	bool Ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
public:
	virtual ~HttpClient() = default;

	virtual HttpResponse Get(const std::string &url, const std::vector<std::string> &headers) = 0;
	virtual HttpResponse Post(const std::string &url, const std::vector<std::string> &headers,
				  const std::string &body, const std::string &contentType) = 0;
	virtual HttpResponse Delete(const std::string &url, const std::vector<std::string> &headers)
	{
		(void)url;
		(void)headers;
		return {0, {}, "DELETE is not supported by this client"};
	}

	// A GET whose 2xx body is handed to onData as it arrives instead of being collected (error bodies are still
	// collected in body). Returning false from onData, or true from interrupt (checked about once a second),
	// ends the request with interrupted set. The default implementation delivers the whole body at once.
	virtual HttpResponse GetStream(const std::string &url, const std::vector<std::string> &headers,
				       const std::function<bool(std::string_view)> &onData,
				       const std::function<bool()> &interrupt)
	{
		(void)interrupt;
		HttpResponse res = Get(url, headers);
		if (res.Ok()) {
			res.interrupted = !onData(res.body);
			res.body.clear();
		}
		return res;
	}
};

} // namespace unified_chat
