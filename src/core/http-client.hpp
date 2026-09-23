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

#include <string>
#include <vector>

namespace unified_chat {

struct HttpResponse {
	long status = 0; // 0 = transport failure, see error
	std::string body;
	std::string error;

	bool Ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
public:
	virtual ~HttpClient() = default;

	virtual HttpResponse Get(const std::string &url, const std::vector<std::string> &headers) = 0;
	virtual HttpResponse Post(const std::string &url, const std::vector<std::string> &headers,
				  const std::string &body, const std::string &contentType) = 0;
};

} // namespace unified_chat
