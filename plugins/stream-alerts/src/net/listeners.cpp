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
#include "core/eventsub.hpp"
#include "core/oauth-device.hpp"
#include "core/platform-events.hpp"
#include "net/account-store.hpp"
#include "net/curl-http-client.hpp"
#include "net/eventsub-socket.hpp"
#include "net/socket-wait.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace stream_alerts {

using namespace unified_chat;
using Clock = std::chrono::steady_clock;

static constexpr int kMaxBackoffMs = 60000;
static constexpr int kQuotaPauseSeconds = 15 * 60;

static int64_t UnixNow()
{
	return (int64_t)std::time(nullptr);
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

// TwitchListener

TwitchListener::TwitchListener(ListenerCallbacks callbacks, TwitchWants wants)
	: Listener(std::move(callbacks)),
	  wants_(wants)
{
	Start();
}

TwitchListener::~TwitchListener()
{
	Stop();
}

TwitchListener::Outcome TwitchListener::Authorize(std::string &userId)
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
		// Each kind needs its own permission; listen for what the sign-in allows and say what's missing.
		allowed_ = wants_;
		std::vector<std::string> missing;
		if (wants_.follows && !user->HasScope(kFollowScope)) {
			allowed_.follows = false;
			missing.push_back("follows");
		}
		if (wants_.notifications && !user->HasScope(kChatScope)) {
			allowed_.notifications = false;
			missing.push_back("subs and raids");
		}
		if (wants_.cheers && !user->HasScope(kBitsScope)) {
			allowed_.cheers = false;
			missing.push_back("cheers");
		}
		missing_.clear();
		for (const auto &kind : missing)
			missing_ += (missing_.empty() ? "" : ", ") + kind;
		if (!allowed_.follows && !allowed_.notifications && !allowed_.cheers) {
			Status("sign in to Twitch again in Tools → OBS Tools → Accounts to allow " + missing_ +
			       " alerts");
			return Outcome::Stop;
		}
		userId = user->id;
		return Outcome::Ok;
	}
	return Outcome::Retry;
}

TwitchListener::Outcome TwitchListener::Subscribe(const std::string &sessionId, const std::string &userId)
{
	CurlHttpClient http(&stop_);
	int subscribed = 0;
	std::string failure;
	for (const auto &body : BuildTwitchSubscriptions(sessionId, userId, allowed_)) {
		HttpResponse res;
		for (int attempt = 0; attempt < 2; ++attempt) {
			res = http.Post(eventsub::kSubscriptionsUrl,
					{"Authorization: Bearer " + account_.token.accessToken,
					 "Client-Id: " + account_.clientId},
					body, "application/json");
			if (res.status == 401 && attempt == 0 && Refresh(Service::Twitch))
				continue;
			break;
		}
		if (res.Ok() || res.status == 409) { // 409: already subscribed on this session
			++subscribed;
			continue;
		}
		failure = HttpError(res, ParseHelixMessage(res.body));
		if (res.status != 401 && res.status != 403)
			break; // a network or server problem: try the whole session again
	}
	if (subscribed > 0 && failure.empty())
		return Outcome::Ok;
	if (subscribed > 0) {
		Status("some Twitch alerts were refused (" + failure + ")");
		return Outcome::Ok;
	}
	Status("Twitch refused the alerts (" + failure + "), retrying");
	return Outcome::Retry;
}

void TwitchListener::Handle(const std::string &type, const nlohmann::json &event)
{
	std::optional<AlertEvent> alert;
	if (type == "channel.follow")
		alert = FollowFromEvent(event);
	else if (type == "channel.chat.notification")
		alert = NotificationFromEvent(event);
	else if (type == "channel.cheer")
		alert = CheerFromEvent(event);
	if (alert && callbacks_.onEvent)
		callbacks_.onEvent(std::move(*alert));
}

TwitchListener::Outcome TwitchListener::Session(const std::string &url, bool resumed, const std::string &userId,
						std::string &reconnectUrl)
{
	EventSubSocket socket(&stop_);
	std::string error;
	if (!socket.Open(url, error)) {
		if (!stop_)
			Status("can't connect to Twitch (" + error + "), retrying");
		return Outcome::Retry;
	}

	auto lastReceive = Clock::now();
	auto keepalive = std::chrono::seconds(eventsub::kKeepaliveSeconds);
	std::string text;

	while (!stop_) {
		// Read everything curl has, including data it already decrypted, before sleeping.
		for (;;) {
			auto read = socket.Next(text);
			if (read == EventSubSocket::Read::Again)
				break;
			if (read == EventSubSocket::Read::Closed) {
				Status("connection to Twitch lost, reconnecting");
				return Outcome::Retry;
			}
			lastReceive = Clock::now();
			auto parsed = eventsub::ParseMessage(text);
			if (!parsed)
				continue;
			switch (parsed->type) {
			case eventsub::Message::Type::Welcome: {
				if (parsed->keepaliveSeconds > 0)
					keepalive = std::chrono::seconds(parsed->keepaliveSeconds);
				// Must subscribe within 10 seconds of the welcome; a reconnect keeps the subscription.
				if (!resumed) {
					Outcome outcome = Subscribe(parsed->sessionId, userId);
					if (outcome != Outcome::Ok)
						return outcome;
				}
				{
					std::string what;
					for (auto [on, name] : {std::pair{allowed_.follows, "follows"},
								std::pair{allowed_.notifications, "subs, gifts, raids"},
								std::pair{allowed_.cheers, "cheers"}}) {
						if (on)
							what += (what.empty() ? "" : ", ") + std::string(name);
					}
					Status("listening for " + what +
					       (missing_.empty() ? std::string()
								 : "; sign in to Twitch again for " + missing_));
				}
				break;
			}
			case eventsub::Message::Type::Notification:
				Handle(parsed->subscriptionType, parsed->event);
				break;
			case eventsub::Message::Type::Reconnect:
				reconnectUrl = parsed->reconnectUrl;
				return reconnectUrl.empty() ? Outcome::Retry : Outcome::Reconnect;
			case eventsub::Message::Type::Revocation:
				Status("Twitch stopped sending " + parsed->subscriptionType + " (" + parsed->status +
				       ")");
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
		socket.Wait(wakeEvent_, (int)waitMs);
	}
	return Outcome::Stop;
}

void TwitchListener::Run()
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
		const std::string url = resumed ? reconnectUrl : std::string(eventsub::kUrl);
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
