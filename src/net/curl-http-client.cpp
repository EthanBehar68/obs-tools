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

#include "curl-http-client.hpp"

#include <curl/curl.h>

namespace unified_chat {

static size_t WriteBody(char *data, size_t size, size_t count, void *userdata)
{
	static_cast<std::string *>(userdata)->append(data, size * count);
	return size * count;
}

static int Progress(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto cancel = static_cast<const std::atomic<bool> *>(userdata);
	return cancel && cancel->load() ? 1 : 0;
}

CurlHttpClient::~CurlHttpClient()
{
	if (curl_)
		curl_easy_cleanup(static_cast<CURL *>(curl_));
}

HttpResponse CurlHttpClient::Get(const std::string &url, const std::vector<std::string> &headers)
{
	return Perform(url, headers, nullptr, {});
}

HttpResponse CurlHttpClient::Post(const std::string &url, const std::vector<std::string> &headers,
				  const std::string &body, const std::string &contentType)
{
	return Perform(url, headers, &body, contentType);
}

HttpResponse CurlHttpClient::Perform(const std::string &url, const std::vector<std::string> &headers,
				     const std::string *body, const std::string &contentType)
{
	HttpResponse response;
	if (!curl_)
		curl_ = curl_easy_init();
	CURL *curl = static_cast<CURL *>(curl_);
	if (!curl) {
		response.error = "curl_easy_init failed";
		return response;
	}

	curl_slist *list = nullptr;
	for (const auto &header : headers)
		list = curl_slist_append(list, header.c_str());
	if (body && !contentType.empty())
		list = curl_slist_append(list, ("Content-Type: " + contentType).c_str());

	char errorBuffer[CURL_ERROR_SIZE] = {};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "obs-unified-chat");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel_);
#ifdef _WIN32
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#endif
	if (body) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body->size());
	}

	CURLcode code = curl_easy_perform(curl);
	if (code == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	} else {
		response.status = 0;
		response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
	}

	// Drops this request's options (they point at locals) but keeps the open connection, TLS session and
	// DNS cache for the next request.
	curl_easy_reset(curl);
	curl_slist_free_all(list);
	return response;
}

} // namespace unified_chat
