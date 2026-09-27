/*
obs-stream-alerts
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

#include "listeners.hpp"
#include "core/alerts-config.hpp"
#include "core/oauth-device.hpp"
#include "core/platform-events.hpp"
#include "net/account-store.hpp"
#include "net/curl-http-client.hpp"
#include "net/socket-wait.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <ctime>

namespace stream_alerts {

using namespace unified_chat;
using Clock = std::chrono::steady_clock;

static constexpr int kMaxBackoffMs = 60000;
static constexpr int kQuotaPauseSeconds = 15 * 60;
static constexpr size_t kMaxMessageBytes = 1 << 20;

static int64_t UnixNow()
{
	return (int64_t)std::time(nullptr);
}

static int AbortOnStop(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<const std::atomic<bool> *>(userdata)->load() ? 1 : 0;
}

static std::string HttpError(const HttpResponse &res, const std::string &reason)
{
	if (res.status == 0)
		return res.error;
	return "HTTP " + std::to_string(res.status) + (reason.empty() ? "" : ", " + reason);
}

// Listener

Listener::Listener(ListenerCallbacks callbacks) : callbacks_(std::move(callbacks)), wakeEvent_(CreateWakeEvent()) {}

Listener::~Listener()
{
	Stop();
	CloseWakeEvent(wakeEvent_);
}

void Listener::Start()
{
	thread_ = std::thread([this] { Run(); });
}

void Listener::Stop()
{
	stop_ = true;
	cv_.notify_all();
	SetWakeEvent(wakeEvent_);
	if (thread_.joinable())
		thread_.join();
}

void Listener::WaitFor(int ms)
{
	std::unique_lock lock(mutex_);
	cv_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stop_.load(); });
}

void Listener::Status(const std::string &status)
{
	if (status == status_)
		return;
	status_ = status;
	if (callbacks_.onStatus)
		callbacks_.onStatus(status);
}

bool Listener::Refresh(Service service)
{
	auto fresh = SharedAccounts().Refresh(service, account_.token, &stop_);
	if (!fresh)
		return false;
	account_.token = *fresh;
	return true;
}

// FollowListener

FollowListener::FollowListener(ListenerCallbacks callbacks) : Listener(std::move(callbacks))
{
	Start();
}

FollowListener::~FollowListener()
{
	Stop();
}

FollowListener::Outcome FollowListener::Authorize(std::string &userId)
{
	account_ = SharedAccounts().Load().twitch;
	if (!account_.token.IsValid() || account_.clientId.empty()) {
		Status("not signed in (Tools → OBS Tools → Accounts)");
		return Outcome::Stop;
	}
	if (account_.token.NeedsRefresh(UnixNow()))
		Refresh(Service::Twitch);

	CurlHttpClient http(&stop_);
	for (int attempt = 0; attempt < 2; ++attempt) {
		auto res = http.Get(oauth::kTwitchValidateUrl, {"Authorization: OAuth " + account_.token.accessToken});
		if (res.status == 401 && attempt == 0 && Refresh(Service::Twitch))
			continue;
		if (res.status == 401) {
			SharedAccounts().Invalidate(Service::Twitch, account_.token);
			Status("sign-in expired: sign in again in Tools → OBS Tools → Accounts");
			return Outcome::Stop;
		}
		if (!res.Ok()) {
			Status("can't reach Twitch (" + HttpError(res, {}) + "), retrying");
			return Outcome::Retry;
		}
		auto user = ParseValidate(res.body);
		if (!user)
			return Outcome::Retry;
		if (!user->HasScope(kFollowScope)) {
			Status("sign in to Twitch again in Tools → OBS Tools → Accounts to allow follower alerts");
			return Outcome::Stop;
		}
		userId = user->id;
		return Outcome::Ok;
	}
	return Outcome::Retry;
}

FollowListener::Outcome FollowListener::Subscribe(const std::string &sessionId, const std::string &userId)
{
	CurlHttpClient http(&stop_);
	for (int attempt = 0; attempt < 2; ++attempt) {
		auto res = http.Post(kSubscriptionsUrl,
				     {"Authorization: Bearer " + account_.token.accessToken,
				      "Client-Id: " + account_.clientId},
				     BuildFollowSubscription(sessionId, userId), "application/json");
		if (res.status == 401 && attempt == 0 && Refresh(Service::Twitch))
			continue;
		if (res.Ok() || res.status == 409) // 409: already subscribed on this session
			return Outcome::Ok;
		const std::string reason = ParseHelixMessage(res.body);
		if (res.status == 401 || res.status == 403) {
			Status("Twitch refused follower alerts (" + HttpError(res, reason) +
			       "): sign in again in Tools → OBS Tools → Accounts");
			return Outcome::Stop;
		}
		Status("couldn't subscribe to follows (" + HttpError(res, reason) + "), retrying");
		return Outcome::Retry;
	}
	return Outcome::Retry;
}

FollowListener::Outcome FollowListener::Session(const std::string &url, bool resumed, const std::string &userId,
						std::string &reconnectUrl)
{
	CURL *curl = curl_easy_init();
	if (!curl)
		return Outcome::Retry;
	char errorBuffer[CURL_ERROR_SIZE] = {};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L); // upgrade to a WebSocket, then hand the connection over
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, AbortOnStop);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stop_);
	struct Cleanup {
		CURL *curl;
		~Cleanup() { curl_easy_cleanup(curl); }
	} cleanup{curl};

	CURLcode rc = curl_easy_perform(curl);
	if (rc != CURLE_OK) {
		if (!stop_)
			Status(std::string("can't connect to Twitch (") +
			       (errorBuffer[0] ? errorBuffer : curl_easy_strerror(rc)) + "), retrying");
		return Outcome::Retry;
	}
	curl_socket_t sock = CURL_SOCKET_BAD;
	curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sock);
	if (sock == CURL_SOCKET_BAD)
		return Outcome::Retry;
	SocketEvent socketEvent(sock);
	if (!socketEvent.Ok())
		return Outcome::Retry;

	auto lastReceive = Clock::now();
	auto keepalive = std::chrono::seconds(kKeepaliveSeconds);
	std::string message;

	while (!stop_) {
		// Read everything curl has, including data it already decrypted, before sleeping.
		for (;;) {
			char buffer[16384];
			size_t received = 0;
			const curl_ws_frame *meta = nullptr;
			rc = curl_ws_recv(curl, buffer, sizeof(buffer), &received, &meta);
			if (rc == CURLE_AGAIN)
				break;
			if (rc != CURLE_OK || !meta) {
				Status("connection to Twitch lost, reconnecting");
				return Outcome::Retry;
			}
			lastReceive = Clock::now();
			if (meta->flags & CURLWS_CLOSE) {
				Status("Twitch closed the connection, reconnecting");
				return Outcome::Retry;
			}
			if (meta->flags & (CURLWS_PING | CURLWS_PONG))
				continue; // curl answers pings itself
			if (message.size() + received > kMaxMessageBytes)
				return Outcome::Retry;
			message.append(buffer, received);
			if (meta->bytesleft > 0 || (meta->flags & CURLWS_CONT))
				continue; // more of this message to come

			auto parsed = ParseEventSubMessage(message);
			message.clear();
			if (!parsed)
				continue;
			switch (parsed->type) {
			case EventSubMessage::Type::Welcome: {
				if (parsed->keepaliveSeconds > 0)
					keepalive = std::chrono::seconds(parsed->keepaliveSeconds);
				// Must subscribe within 10 seconds of the welcome; a reconnect keeps the subscription.
				if (!resumed) {
					Outcome outcome = Subscribe(parsed->sessionId, userId);
					if (outcome != Outcome::Ok)
						return outcome;
				}
				Status("listening for follows");
				break;
			}
			case EventSubMessage::Type::Notification:
				if (parsed->subscriptionType == "channel.follow" && !parsed->userId.empty() &&
				    callbacks_.onEvent)
					callbacks_.onEvent({AlertKind::Follow, parsed->userId, parsed->userName});
				break;
			case EventSubMessage::Type::Reconnect:
				reconnectUrl = parsed->reconnectUrl;
				return reconnectUrl.empty() ? Outcome::Retry : Outcome::Reconnect;
			case EventSubMessage::Type::Revocation:
				Status("Twitch ended follower alerts (" + parsed->status + ")");
				return parsed->status == "authorization_revoked" || parsed->status == "user_removed"
					       ? Outcome::Stop
					       : Outcome::Retry;
			default:
				break;
			}
		}

		// Twitch sends something at least every keepalive interval; silence past it means a dead connection.
		auto deadline = lastReceive + keepalive + std::chrono::seconds(10);
		auto now = Clock::now();
		if (now >= deadline) {
			Status("connection to Twitch timed out, reconnecting");
			return Outcome::Retry;
		}
		auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() + 1;
		WaitForActivity(socketEvent, wakeEvent_, sock, (int)waitMs);
	}
	return Outcome::Stop;
}

void FollowListener::Run()
{
	int backoffMs = 1000;
	std::string userId;
	std::string reconnectUrl;
	while (!stop_) {
		const bool resumed = !reconnectUrl.empty();
		if (!resumed) {
			Status("connecting");
			Outcome outcome = Authorize(userId);
			if (outcome == Outcome::Stop)
				return;
			if (outcome == Outcome::Retry) {
				WaitFor(backoffMs);
				backoffMs = (std::min)(backoffMs * 2, kMaxBackoffMs);
				continue;
			}
		}
		const std::string url = resumed ? reconnectUrl : std::string(kEventSubUrl);
		reconnectUrl.clear();
		const auto started = Clock::now();
		Outcome outcome = Session(url, resumed, userId, reconnectUrl);
		if (outcome == Outcome::Stop)
			return;
		if (outcome == Outcome::Reconnect)
			continue; // Twitch asked; the new URL keeps the subscription
		if (Clock::now() - started > std::chrono::minutes(5))
			backoffMs = 1000; // it had been working
		WaitFor(backoffMs);
		backoffMs = (std::min)(backoffMs * 2, kMaxBackoffMs);
	}
}

// SubscriberPoller

SubscriberPoller::SubscriberPoller(ListenerCallbacks callbacks) : Listener(std::move(callbacks))
{
	Start();
}

SubscriberPoller::~SubscriberPoller()
{
	Stop();
}

void SubscriberPoller::Run()
{
	SubscriberWatch watch;
	CurlHttpClient http(&stop_);
	while (!stop_) {
		int waitSeconds = kSubscriberCheckSeconds;
		account_ = SharedAccounts().Load().google; // picks up a refresh the chat plugin made
		if (!account_.token.IsValid()) {
			Status("not signed in (Tools → OBS Tools → Accounts)");
			return;
		}
		if (account_.token.NeedsRefresh(UnixNow()))
			Refresh(Service::Google);

		HttpResponse res;
		for (int attempt = 0; attempt < 2; ++attempt) {
			res = http.Get(RecentSubscribersUrl(), {"Authorization: Bearer " + account_.token.accessToken});
			if (res.status == 401 && attempt == 0 && Refresh(Service::Google))
				continue;
			break;
		}
		const std::string reason = oauth::ParseGoogleErrorReason(res.body);
		if (res.status == 401) {
			SharedAccounts().Invalidate(Service::Google, account_.token);
			Status("sign-in expired: sign in again in Tools → OBS Tools → Accounts");
			return;
		}
		if (reason == "quotaExceeded" || reason == "rateLimitExceeded" || res.status == 429) {
			Status("YouTube API quota exceeded, pausing for 15 minutes");
			waitSeconds = kQuotaPauseSeconds;
		} else if (!res.Ok()) {
			Status("can't check subscribers (" + HttpError(res, reason) + "), retrying");
		} else if (auto subscribers = ParseRecentSubscribers(res.body)) {
			for (auto &subscriber : watch.Update(*subscribers)) {
				if (callbacks_.onEvent)
					callbacks_.onEvent({AlertKind::Subscriber, subscriber.channelId,
							    std::move(subscriber.name)});
			}
			Status("watching for subscribers");
		}
		WaitFor(waitSeconds * 1000);
	}
}

} // namespace stream_alerts
