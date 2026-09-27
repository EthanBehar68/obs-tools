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

#include <curl/curl.h>

namespace unified_chat {

// Waits on a socket curl opened with CURLOPT_CONNECT_ONLY. A long-lived connection thread sleeps in
// WaitForActivity until data arrives, its wake event is set, or its next deadline, so an idle connection costs
// nothing. curl can hold decrypted data the socket no longer shows, so always read to CURLE_AGAIN first.

bool WaitSocket(curl_socket_t sock, bool forRead, int timeoutMs);

// Signals when the socket has data or is closed (Windows: WSAEventSelect). The association is cancelled when this
// goes away.
class SocketEvent {
public:
	explicit SocketEvent(curl_socket_t sock);
	~SocketEvent();
	SocketEvent(const SocketEvent &) = delete;
	SocketEvent &operator=(const SocketEvent &) = delete;

	bool Ok() const { return ok_; }
	void *Handle() const { return event_; }

private:
	curl_socket_t sock_;
	void *event_ = nullptr;
	bool ok_ = false;
};

// Blocks until the socket has data, wakeEvent (a Windows event handle, may be null) is set, or timeoutMs passes.
void WaitForActivity(const SocketEvent &socketEvent, void *wakeEvent, curl_socket_t sock, int timeoutMs);

// An auto-reset Windows event for waking a connection thread from another thread (null elsewhere).
void *CreateWakeEvent();
void SetWakeEvent(void *event);
void CloseWakeEvent(void *event);

} // namespace unified_chat
