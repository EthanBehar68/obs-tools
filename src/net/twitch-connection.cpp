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

#include "twitch-connection.hpp"
#include "curl-http-client.hpp"
#include "core/twitch-irc.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

namespace unified_chat {

using Clock = std::chrono::steady_clock;

static constexpr auto kStaleConnection = std::chrono::minutes(6);
static constexpr auto kValidateInterval = std::chrono::hours(1);
static constexpr int kMaxBackoffMs = 60000;

static int64_t UnixNow()
{
	return (int64_t)std::time(nullptr);
}

static int AbortOnStop(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<const std::atomic<bool> *>(userdata)->load() ? 1 : 0;
}

static bool WaitSocket(curl_socket_t sock, bool forRead, int timeoutMs)
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

static bool SendLine(CURL *curl, curl_socket_t sock, const std::string &line)
{
	std::string data = line + "\r\n";
	size_t offset = 0;
	while (offset < data.size()) {
		size_t sent = 0;
		CURLcode rc = curl_easy_send(curl, data.data() + offset, data.size() - offset, &sent);
		if (rc == CURLE_AGAIN) {
			WaitSocket(sock, false, 1000);
			continue;
		}
		if (rc != CURLE_OK)
			return false;
		offset += sent;
	}
	return true;
}

TwitchConnection::TwitchConnection(std::string channel, std::string clientId, std::string login, oauth::Token token,
				   ConnectionCallbacks callbacks)
	: channel_(std::move(channel)),
	  clientId_(std::move(clientId)),
	  login_(std::move(login)),
	  token_(std::move(token)),
	  callbacks_(std::move(callbacks))
{
	thread_ = std::thread(&TwitchConnection::Run, this);
}

TwitchConnection::~TwitchConnection()
{
	stop_ = true;
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

void TwitchConnection::Send(std::string text, uint64_t sendId)
{
	{
		std::lock_guard lock(mutex_);
		outgoing_.push_back({std::move(text), sendId});
	}
	cv_.notify_all();
}

void TwitchConnection::Notice(const std::string &text)
{
	if (callbacks_.onNotice)
		callbacks_.onNotice("Twitch: " + text);
}

void TwitchConnection::SetState(LinkState state)
{
	if (callbacks_.onState)
		callbacks_.onState(state);
}

void TwitchConnection::WaitFor(int ms)
{
	std::unique_lock lock(mutex_);
	cv_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stop_.load(); });
}

bool TwitchConnection::RefreshToken()
{
	if (token_.refreshToken.empty() || clientId_.empty())
		return false;

	CurlHttpClient http(&stop_);
	auto provider = oauth::TwitchProvider(clientId_);
	auto res = http.Post(provider.tokenUrl, {}, oauth::BuildRefreshBody(provider, token_.refreshToken),
			     "application/x-www-form-urlencoded");
	auto parsed = oauth::ParseTokenResponse(res.status, res.body, UnixNow(), token_.refreshToken);
	if (parsed.status != oauth::PollStatus::Granted)
		return false;

	token_ = parsed.token;
	if (callbacks_.onTokenChanged)
		callbacks_.onTokenChanged(token_, login_);
	return true;
}

bool TwitchConnection::ValidateToken(bool allowRefresh)
{
	if (!token_.IsValid())
		return false;
	if (token_.NeedsRefresh(UnixNow()) && !RefreshToken())
		allowRefresh = false;

	CurlHttpClient http(&stop_);
	auto res = http.Get(twitch::kValidateUrl, {"Authorization: OAuth " + token_.accessToken});
	if (res.status == 0)
		return true; // offline; the IRC login will report a bad token
	if (res.Ok()) {
		std::string login = twitch::ParseValidateLogin(res.body);
		if (!login.empty() && login != login_) {
			login_ = login;
			if (callbacks_.onTokenChanged)
				callbacks_.onTokenChanged(token_, login_);
		}
		return true;
	}
	if (res.status == 401 && allowRefresh && RefreshToken())
		return ValidateToken(false);

	token_ = {};
	if (callbacks_.onTokenChanged)
		callbacks_.onTokenChanged(token_, login_);
	Notice("sign-in expired, reading chat anonymously. Sign in again from the chat settings to send messages");
	return false;
}

void TwitchConnection::RunSession(void *handle)
{
	CURL *curl = static_cast<CURL *>(handle);
	curl_socket_t sock = CURL_SOCKET_BAD;
	curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sock);
	if (sock == CURL_SOCKET_BAD)
		return;

	std::random_device random;
	twitch::IrcSession session(channel_, login_, token_.accessToken, 10000 + random() % 90000);
	twitch::LineBuffer buffer;

	for (const auto &line : session.Start()) {
		if (!SendLine(curl, sock, line))
			return;
	}

	auto lastReceive = Clock::now();
	auto lastValidate = Clock::now();
	bool wasJoined = false;

	while (!stop_) {
		std::deque<OutgoingMessage> pending;
		{
			std::lock_guard lock(mutex_);
			pending.swap(outgoing_);
		}
		for (const auto &outgoing : pending) {
			auto failed = [&]() {
				if (callbacks_.onSendFailed)
					callbacks_.onSendFailed(outgoing.sendId);
			};
			if (!session.CanSend()) {
				Notice(session.IsAuthenticated() ? "not joined yet, message not sent"
								 : "sign in to send messages");
				failed();
				continue;
			}
			auto line = session.BuildPrivmsg(outgoing.text);
			if (!line) {
				failed();
				continue;
			}
			if (!SendLine(curl, sock, *line)) {
				Notice("message not sent, connection lost");
				failed();
				return;
			}
			if (callbacks_.onMessage) {
				ChatMessage echo = session.LocalEcho(outgoing.text);
				echo.sendId = outgoing.sendId;
				callbacks_.onMessage(echo);
			}
		}

		char data[16384];
		size_t received = 0;
		CURLcode rc = curl_easy_recv(curl, data, sizeof(data), &received);
		if (rc == CURLE_AGAIN) {
			WaitSocket(sock, true, 250);
		} else if (rc != CURLE_OK || received == 0) {
			Notice("disconnected");
			return;
		} else {
			lastReceive = Clock::now();
			for (const auto &line : buffer.Append(std::string_view(data, received))) {
				twitch::SessionOutput out;
				session.HandleLine(line, out);
				for (const auto &reply : out.outgoing)
					SendLine(curl, sock, reply);
				for (const auto &message : out.messages) {
					if (callbacks_.onMessage)
						callbacks_.onMessage(message);
				}
				for (const auto &notice : out.notices)
					Notice(notice);
				if (out.authFailed) {
					if (!RefreshToken()) {
						token_ = {};
						if (callbacks_.onTokenChanged)
							callbacks_.onTokenChanged(token_, login_);
						Notice("login rejected, reading chat anonymously");
					}
					return;
				}
				if (out.reconnect) {
					Notice("server requested a reconnect");
					return;
				}
			}
			if (session.IsJoined() && !wasJoined) {
				wasJoined = true;
				SetState(session.CanSend() ? LinkState::Connected : LinkState::ReadOnly);
				Notice("joined #" + session.Channel() + (session.CanSend() ? "" : " (read-only)"));
			}
		}

		if (Clock::now() - lastReceive > kStaleConnection) {
			Notice("connection timed out");
			return;
		}
		if (token_.IsValid() && Clock::now() - lastValidate > kValidateInterval) {
			lastValidate = Clock::now();
			if (!ValidateToken(true))
				return;
		}
	}
}

void TwitchConnection::Run()
{
	if (twitch::NormalizeChannel(channel_).empty()) {
		Notice("no channel configured, open the chat settings to set one");
		SetState(LinkState::Disconnected);
		return;
	}

	int backoffMs = 1000;
	while (!stop_) {
		if (token_.IsValid())
			ValidateToken(true);

		SetState(LinkState::Connecting);
		CURL *curl = curl_easy_init();
		char errorBuffer[CURL_ERROR_SIZE] = {};
		curl_easy_setopt(curl, CURLOPT_URL, twitch::kIrcUrl);
		curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, 0L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, AbortOnStop);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stop_);
#ifdef _WIN32
		curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#endif

		CURLcode rc = curl_easy_perform(curl);
		auto started = Clock::now();
		if (rc == CURLE_OK)
			RunSession(curl);
		else
			Notice(std::string("connection failed: ") +
			       (errorBuffer[0] ? errorBuffer : curl_easy_strerror(rc)));
		curl_easy_cleanup(curl);

		SetState(LinkState::Disconnected);
		if (stop_)
			break;

		// A session that stayed up for a while resets the backoff.
		if (rc == CURLE_OK && Clock::now() - started > std::chrono::seconds(30))
			backoffMs = 1000;
		WaitFor(backoffMs);
		backoffMs = (std::min)(backoffMs * 2, kMaxBackoffMs);
	}
}

} // namespace unified_chat
