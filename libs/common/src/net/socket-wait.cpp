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

#include "socket-wait.hpp"

#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

namespace unified_chat {

bool WaitSocket(curl_socket_t sock, bool forRead, int timeoutMs)
{
	fd_set set;
	fd_set errors;
	FD_ZERO(&set);
	FD_ZERO(&errors);
	FD_SET(sock, &set);
	FD_SET(sock, &errors);
	timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
	int rc = select((int)sock + 1, forRead ? &set : nullptr, forRead ? nullptr : &set, &errors, &tv);
	return rc > 0;
}

#ifdef _WIN32

SocketEvent::SocketEvent(curl_socket_t sock) : sock_(sock)
{
	WSAEVENT event = WSACreateEvent();
	if (event == WSA_INVALID_EVENT)
		return;
	event_ = event;
	ok_ = WSAEventSelect(sock_, event, FD_READ | FD_CLOSE) == 0;
}

SocketEvent::~SocketEvent()
{
	if (!event_)
		return;
	WSAEventSelect(sock_, static_cast<WSAEVENT>(event_), 0);
	WSACloseEvent(static_cast<WSAEVENT>(event_));
}

void WaitForActivity(const SocketEvent &socketEvent, void *wakeEvent, curl_socket_t, int timeoutMs)
{
	HANDLE handles[] = {static_cast<HANDLE>(socketEvent.Handle()), static_cast<HANDLE>(wakeEvent)};
	if (wakeEvent)
		WaitForMultipleObjects(2, handles, FALSE, (DWORD)timeoutMs);
	else // no wake event: fall back to noticing sends and shutdown by timeout
		WaitForSingleObject(handles[0], (DWORD)(std::min)(timeoutMs, 250));
	WSAResetEvent(static_cast<WSAEVENT>(socketEvent.Handle()));
}

void *CreateWakeEvent()
{
	return CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

void SetWakeEvent(void *event)
{
	if (event)
		SetEvent(static_cast<HANDLE>(event));
}

void CloseWakeEvent(void *event)
{
	if (event)
		CloseHandle(static_cast<HANDLE>(event));
}

#else

SocketEvent::SocketEvent(curl_socket_t sock) : sock_(sock), ok_(true) {}
SocketEvent::~SocketEvent() = default;

void WaitForActivity(const SocketEvent &, void *, curl_socket_t sock, int timeoutMs)
{
	WaitSocket(sock, true, (std::min)(timeoutMs, 250));
}

void *CreateWakeEvent()
{
	return nullptr;
}
void SetWakeEvent(void *) {}
void CloseWakeEvent(void *) {}

#endif

} // namespace unified_chat
