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

namespace {

// Per-request state shared with curl's callbacks.
struct Transfer {
	CURL *curl;
	std::string *body;
	const std::function<bool(std::string_view)> *onData; // streaming only
	const std::function<bool()> *interrupt;              // streaming only
	const std::atomic<bool> *cancel;
	bool interrupted = false;
};

size_t WriteBody(char *data, size_t size, size_t count, void *userdata)
{
	auto transfer = static_cast<Transfer *>(userdata);
	const size_t bytes = size * count;
	if (transfer->onData && *transfer->onData) {
		long status = 0;
		curl_easy_getinfo(transfer->curl, CURLINFO_RESPONSE_CODE, &status);
		if (status >= 200 && status < 300) {
			if (!(*transfer->onData)(std::string_view(data, bytes))) {
				transfer->interrupted = true;
				return 0; // aborts the transfer
			}
			return bytes;
		}
	}
	transfer->body->append(data, bytes);
	return bytes;
}

// curl calls this at least about once a second, also while a stream is idle.
int Progress(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto transfer = static_cast<Transfer *>(userdata);
	if (transfer->cancel && transfer->cancel->load())
		return 1;
	if (transfer->interrupt && *transfer->interrupt && (*transfer->interrupt)()) {
		transfer->interrupted = true;
		return 1;
	}
	return 0;
}

} // namespace

CurlHttpClient::~CurlHttpClient()
{
	if (curl_)
		curl_easy_cleanup(static_cast<CURL *>(curl_));
}

HttpResponse CurlHttpClient::Get(const std::string &url, const std::vector<std::string> &headers)
{
	return Perform(url, headers, nullptr, {}, nullptr, nullptr);
}

HttpResponse CurlHttpClient::Post(const std::string &url, const std::vector<std::string> &headers,
				  const std::string &body, const std::string &contentType)
{
	return Perform(url, headers, &body, contentType, nullptr, nullptr);
}

HttpResponse CurlHttpClient::Delete(const std::string &url, const std::vector<std::string> &headers)
{
	return Perform(url, headers, nullptr, {}, nullptr, nullptr, true);
}

HttpResponse CurlHttpClient::GetStream(const std::string &url, const std::vector<std::string> &headers,
				       const std::function<bool(std::string_view)> &onData,
				       const std::function<bool()> &interrupt)
{
	return Perform(url, headers, nullptr, {}, &onData, &interrupt);
}

HttpResponse CurlHttpClient::Perform(const std::string &url, const std::vector<std::string> &headers,
				     const std::string *body, const std::string &contentType,
				     const std::function<bool(std::string_view)> *onData,
				     const std::function<bool()> *interrupt, bool deleteMethod)
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

	Transfer transfer{curl, &response.body, onData, interrupt, cancel_};
	char errorBuffer[CURL_ERROR_SIZE] = {};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "obs-unified-chat");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	// A chat stream stays open until the server ends it (about 10 s), so it gets a longer limit.
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, onData ? 90L : 20L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
#ifdef _WIN32
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#endif
	if (deleteMethod)
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	if (body) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body->size());
	}

	CURLcode code = curl_easy_perform(curl);
	if (code == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	} else if (transfer.interrupted) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
		response.interrupted = true;
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
