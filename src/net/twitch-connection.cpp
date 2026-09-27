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
#include "core/text-util.hpp"
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
static constexpr auto kViewerCheckInterval = std::chrono::minutes(5);
static constexpr int kMaxReadsPerDrain = 32; // 16 KB each; a flood can't hold up outgoing messages

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

#ifdef _WIN32
// Signals when the socket has data or is closed. The association is cancelled when the session ends.
class SocketEvent {
public:
	explicit SocketEvent(curl_socket_t sock) : sock_(sock), event_(WSACreateEvent())
	{
		ok_ = event_ != WSA_INVALID_EVENT && WSAEventSelect(sock_, event_, FD_READ | FD_CLOSE) == 0;
	}
	~SocketEvent()
	{
		if (event_ == WSA_INVALID_EVENT)
			return;
		WSAEventSelect(sock_, event_, 0);
		WSACloseEvent(event_);
	}
	SocketEvent(const SocketEvent &) = delete;
	SocketEvent &operator=(const SocketEvent &) = delete;

	bool Ok() const { return ok_; }
	WSAEVENT Handle() const { return event_; }

private:
	curl_socket_t sock_;
	WSAEVENT event_;
	bool ok_ = false;
};
#else
class SocketEvent {
public:
	explicit SocketEvent(curl_socket_t) {}
	bool Ok() const { return true; }
};
#endif

// Blocks until the socket has data, wakeEvent is set, or timeoutMs passes.
static void WaitForActivity(const SocketEvent &socketEvent, void *wakeEvent, curl_socket_t sock, int timeoutMs)
{
#ifdef _WIN32
	(void)sock;
	HANDLE handles[] = {socketEvent.Handle(), wakeEvent};
	if (wakeEvent)
		WaitForMultipleObjects(2, handles, FALSE, (DWORD)timeoutMs);
	else // no wake event: fall back to noticing sends and shutdown by timeout
		WaitForSingleObject(handles[0], (DWORD)(std::min)(timeoutMs, 250));
	WSAResetEvent(socketEvent.Handle());
#else
	(void)socketEvent;
	(void)wakeEvent;
	WaitSocket(sock, true, (std::min)(timeoutMs, 250));
#endif
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
#ifdef _WIN32
	wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
#endif
	thread_ = std::thread(&TwitchConnection::Run, this);
}

TwitchConnection::~TwitchConnection()
{
	stop_ = true;
	Wake();
	if (thread_.joinable())
		thread_.join();
#ifdef _WIN32
	if (wakeEvent_)
		CloseHandle(wakeEvent_);
#endif
}

void TwitchConnection::Wake()
{
	cv_.notify_all();
#ifdef _WIN32
	if (wakeEvent_)
		SetEvent(wakeEvent_);
#endif
}

void TwitchConnection::Send(std::string text, uint64_t sendId, std::string replyParentId, std::string replyTo)
{
	{
		std::lock_guard lock(mutex_);
		outgoing_.push_back({std::move(text), sendId, std::move(replyParentId), std::move(replyTo)});
	}
	Wake();
}

void TwitchConnection::Moderate(ModerationAction action)
{
	{
		std::lock_guard lock(mutex_);
		moderation_.push_back(std::move(action));
	}
	Wake();
}

void TwitchConnection::Moderate(const ModerationAction &action, const std::string &channelId)
{
	// Runs on the IRC thread; a Helix call takes a fraction of a second and moderation is rare.
	if (!token_.IsValid()) {
		Notice("sign in to moderate from the dock");
		return;
	}
	const TwitchIdentity &identity = identity_; // set by ValidateToken on this same thread
	if (!identity.CanModerate()) {
		Notice("sign in to Twitch again (Settings → Twitch → Sign in) to allow moderating from the dock");
		return;
	}
	auto request = BuildTwitchModeration(action, channelId, identity.userId);
	if (!request) {
		Notice("can't moderate yet: the channel isn't joined");
		return;
	}

	CurlHttpClient http(&stop_);
	auto perform = [&]() {
		std::vector<std::string> headers{"Authorization: Bearer " + token_.accessToken,
						 "Client-Id: " + clientId_};
		return request->method == HttpRequest::Method::Delete
			       ? http.Delete(request->url, headers)
			       : http.Post(request->url, headers, request->body, "application/json");
	};
	HttpResponse res = perform();
	if (res.status == 401 && RefreshToken())
		res = perform();
	if (!res.Ok()) {
		const std::string reason = ModerationErrorMessage(res.body);
		Notice("couldn't moderate " + action.userName + " (" +
		       (res.status == 0
				? res.error
				: "HTTP " + std::to_string(res.status) + (reason.empty() ? "" : ", " + reason)) +
		       ")");
		return;
	}
	// Deletions, timeouts and bans come back through chat (CLEARMSG / CLEARCHAT); an unban doesn't.
	if (action.kind == ModerationAction::Kind::Unban)
		Notice(action.userName + " was unbanned");
}

void TwitchConnection::CheckViewers(const std::string &channelId)
{
	CurlHttpClient http(&stop_);
	auto res = http.Get("https://api.twitch.tv/helix/streams?user_id=" + UrlEncode(channelId),
			    {"Authorization: Bearer " + token_.accessToken, "Client-Id: " + clientId_});
	if (!res.Ok())
		return; // try again at the next check; a count isn't worth a notice
	if (auto viewers = twitch::ParseStreamViewerCount(res.body); viewers && callbacks_.onViewers)
		callbacks_.onViewers(*viewers);
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
		// Keeps the user id and granted scopes too: moderation needs both.
		if (auto identity = ParseTwitchIdentity(res.body))
			identity_ = std::move(*identity);
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
	SocketEvent socketEvent(sock);
	if (!socketEvent.Ok()) {
		Notice("could not watch the connection for incoming data");
		return;
	}

	std::random_device random;
	twitch::IrcSession session(channel_, login_, token_.accessToken, 10000 + random() % 90000);
	twitch::LineBuffer buffer;

	for (const auto &line : session.Start()) {
		if (!SendLine(curl, sock, line))
			return;
	}

	auto lastReceive = Clock::now();
	auto lastValidate = Clock::now();
	auto nextViewerCheck = Clock::now(); // soon after joining, then every kViewerCheckInterval
	bool wasJoined = false;

	while (!stop_) {
		std::deque<OutgoingMessage> pending;
		std::deque<ModerationAction> actions;
		{
			std::lock_guard lock(mutex_);
			pending.swap(outgoing_);
			actions.swap(moderation_);
		}
		for (const auto &action : actions)
			Moderate(action, session.ChannelId());
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
			auto line = session.BuildPrivmsg(outgoing.text, outgoing.replyParentId);
			if (!line) {
				failed();
				continue;
			}
			if (!SendLine(curl, sock, *line)) {
				Notice("message not sent, connection lost");
				failed();
				return;
			}
			if (callbacks_.onMessages) {
				ChatMessage echo = session.LocalEcho(outgoing.text, outgoing.replyTo);
				echo.sendId = outgoing.sendId;
				callbacks_.onMessages({std::move(echo)});
			}
		}

		// Read until curl reports nothing left. That includes data Schannel has already decrypted and
		// buffered, which the socket event can't see; blocking before draining it could stall chat.
		// Everything read here reaches the dock as one batch; a notice flushes first to keep order.
		std::vector<ChatMessage> batch;
		auto flush = [&]() {
			if (!batch.empty() && callbacks_.onMessages)
				callbacks_.onMessages(std::move(batch));
			batch.clear();
		};
		bool drained = false;
		for (int reads = 0; reads < kMaxReadsPerDrain; ++reads) {
			char data[16384];
			size_t received = 0;
			CURLcode rc = curl_easy_recv(curl, data, sizeof(data), &received);
			if (rc == CURLE_AGAIN) {
				drained = true;
				break;
			}
			if (rc != CURLE_OK || received == 0) {
				flush();
				Notice("disconnected");
				return;
			}
			lastReceive = Clock::now();
			for (const auto &line : buffer.Append(std::string_view(data, received))) {
				twitch::SessionOutput out;
				session.HandleLine(line, out);
				for (const auto &reply : out.outgoing)
					SendLine(curl, sock, reply);
				for (auto &message : out.messages)
					batch.push_back(std::move(message));
				if (!out.notices.empty() || !out.moderation.empty() || out.authFailed || out.reconnect)
					flush();
				for (auto &event : out.moderation) {
					if (callbacks_.onModeration)
						callbacks_.onModeration(std::move(event));
				}
				if (!out.channelId.empty() && callbacks_.onChannelId)
					callbacks_.onChannelId(out.channelId);
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
		}
		flush();
		if (session.IsJoined() && !wasJoined) {
			wasJoined = true;
			SetState(session.CanSend() ? LinkState::Connected : LinkState::ReadOnly);
			Notice("joined #" + session.Channel() + (session.CanSend() ? "" : " (read-only)"));
		}

		auto now = Clock::now();
		if (now - lastReceive > kStaleConnection) {
			Notice("connection timed out");
			return;
		}
		// The viewer count needs the API (a sign-in) and the channel's id, known once joined.
		const bool canCountViewers = token_.IsValid() && session.IsJoined() && !session.ChannelId().empty();
		if (canCountViewers && now >= nextViewerCheck) {
			nextViewerCheck = now + kViewerCheckInterval;
			CheckViewers(session.ChannelId());
		}
		if (token_.IsValid() && now - lastValidate > kValidateInterval) {
			lastValidate = now;
			if (!ValidateToken(true))
				return;
		}

		// Still more to read (a flood): loop straight back so queued sends go out, without blocking.
		if (!drained)
			continue;

		// Sleep until data arrives, Send() or shutdown wakes us, or the next housekeeping check is due.
		// Twitch PINGs every few minutes, so an idle connection wakes only a handful of times an hour.
		auto due = lastReceive + kStaleConnection;
		if (token_.IsValid())
			due = (std::min)(due, lastValidate + kValidateInterval);
		if (canCountViewers)
			due = (std::min)(due, nextViewerCheck);
		auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(due - Clock::now()).count() + 1;
		WaitForActivity(socketEvent, wakeEvent_, sock, (int)std::clamp<long long>(waitMs, 0, 60 * 60 * 1000));
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
