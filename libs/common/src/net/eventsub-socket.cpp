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

#include "eventsub-socket.hpp"

#include <curl/curl.h>

namespace unified_chat {

static constexpr size_t kMaxMessageBytes = 1 << 20;

static int AbortOnStop(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto stop = static_cast<const std::atomic<bool> *>(userdata);
	return stop && stop->load() ? 1 : 0;
}

EventSubSocket::EventSubSocket(const std::atomic<bool> *stop) : stop_(stop) {}

EventSubSocket::~EventSubSocket()
{
	event_.reset(); // before the socket closes
	if (curl_)
		curl_easy_cleanup(static_cast<CURL *>(curl_));
}

bool EventSubSocket::Open(const std::string &url, std::string &error)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		error = "curl_easy_init failed";
		return false;
	}
	curl_ = curl;
	char errorBuffer[CURL_ERROR_SIZE] = {};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L); // upgrade to a WebSocket, then hand the connection over
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, AbortOnStop);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, stop_);
	CURLcode rc = curl_easy_perform(curl);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, nullptr); // the buffer goes out of scope
	if (rc != CURLE_OK) {
		error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(rc);
		return false;
	}
	curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sock_);
	if (sock_ == CURL_SOCKET_BAD) {
		error = "no socket";
		return false;
	}
	event_ = std::make_unique<SocketEvent>(sock_);
	if (!event_->Ok()) {
		error = "could not watch the connection";
		return false;
	}
	return true;
}

EventSubSocket::Read EventSubSocket::Next(std::string &message)
{
	if (!curl_ || !event_)
		return Read::Closed;
	CURL *curl = static_cast<CURL *>(curl_);
	for (;;) {
		char buffer[16384];
		size_t received = 0;
		const curl_ws_frame *meta = nullptr;
		CURLcode rc = curl_ws_recv(curl, buffer, sizeof(buffer), &received, &meta);
		if (rc == CURLE_AGAIN)
			return Read::Again;
		if (rc != CURLE_OK || !meta || (meta->flags & CURLWS_CLOSE))
			return Read::Closed;
		if (meta->flags & (CURLWS_PING | CURLWS_PONG))
			continue;
		if (partial_.size() + received > kMaxMessageBytes)
			return Read::Closed;
		partial_.append(buffer, received);
		if (meta->bytesleft > 0 || (meta->flags & CURLWS_CONT))
			continue; // more of this message to come
		message = std::move(partial_);
		partial_.clear();
		return Read::Message;
	}
}

void EventSubSocket::Wait(void *wakeEvent, int timeoutMs)
{
	if (event_)
		WaitForActivity(*event_, wakeEvent, sock_, timeoutMs);
}

} // namespace unified_chat
