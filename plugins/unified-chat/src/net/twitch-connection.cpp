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
#include "net/account-store.hpp"
#include "net/curl-http-client.hpp"
#include "net/eventsub-client.hpp"
#include "net/socket-wait.hpp"
#include "core/eventsub.hpp"
#include "core/text-util.hpp"
#include "core/twitch-eventsub.hpp"
#include "core/twitch-irc.hpp"

#include <curl/curl.h>
#include <obs-module.h>
#include <plugin-support.h>

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
static constexpr int kMaxReadsPerDrain = 32; // 16 KB each; a flood can't hold up outgoing messages
static constexpr size_t kMaxSentIds = 16;

static int64_t UnixNow()
{
	return (int64_t)std::time(nullptr);
}

static int AbortOnStop(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<const std::atomic<bool> *>(userdata)->load() ? 1 : 0;
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
				   bool useEventSub, int viewerCheckMinutes, ConnectionCallbacks callbacks)
	: channel_(std::move(channel)),
	  clientId_(std::move(clientId)),
	  login_(std::move(login)),
	  token_(std::move(token)),
	  callbacks_(std::move(callbacks)),
	  useEventSub_(useEventSub),
	  viewerCheckInterval_(viewerCheckMinutes)
{
	wakeEvent_ = CreateWakeEvent();
	thread_ = std::thread(&TwitchConnection::Run, this);
}

TwitchConnection::~TwitchConnection()
{
	stop_ = true;
	Wake();
	if (thread_.joinable())
		thread_.join();
	CloseWakeEvent(wakeEvent_);
}

void TwitchConnection::Wake()
{
	cv_.notify_all();
	SetWakeEvent(wakeEvent_);
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

void TwitchConnection::Moderate(const ModerationAction &action, const std::string &channelId, HttpClient &http)
{
	// Runs on the connection thread; a Helix call takes a fraction of a second and moderation is rare.
	if (!token_.IsValid()) {
		Notice("sign in to moderate from the dock");
		return;
	}
	const TwitchIdentity &identity = identity_; // set by ValidateToken on this same thread
	if (!identity.CanModerate()) {
		Notice("sign in to Twitch again (Tools → OBS Tools → Accounts) to allow moderating from the dock");
		return;
	}
	auto request = BuildTwitchModeration(action, channelId, identity.userId);
	if (!request) {
		Notice("can't moderate yet: the channel isn't joined");
		return;
	}

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
	// Deletions, timeouts and bans come back through chat (IRC CLEARMSG / CLEARCHAT, or EventSub); an unban doesn't.
	if (action.kind == ModerationAction::Kind::Unban)
		Notice(action.userName + " was unbanned");
}

void TwitchConnection::CheckViewers(const std::string &channelId, HttpClient &http)
{
	auto res = http.Get("https://api.twitch.tv/helix/streams?user_id=" + UrlEncode(channelId),
			    {"Authorization: Bearer " + token_.accessToken, "Client-Id: " + clientId_});
	if (!res.Ok())
		return; // try again at the next check; a count isn't worth a notice
	if (auto viewers = twitch::ParseStreamViewerCount(res.body); viewers && callbacks_.onViewers)
		callbacks_.onViewers(*viewers);
}

void TwitchConnection::EventNotice(const std::string &text)
{
	if (callbacks_.onEventNotice)
		callbacks_.onEventNotice("Twitch: " + text);
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

bool TwitchConnection::RefreshToken(bool *rejected)
{
	// Through the shared store: another plugin may already have spent this single-use refresh token, and then
	// its new token is the one to use.
	auto fresh = SharedAccounts().Refresh(Service::Twitch, token_, &stop_);
	if (rejected)
		*rejected = fresh.Rejected();
	if (!fresh.Granted())
		return false;

	token_ = fresh.token;
	if (callbacks_.onTokenChanged)
		callbacks_.onTokenChanged(token_, login_);
	return true;
}

bool TwitchConnection::ValidateToken(bool allowRefresh)
{
	if (!token_.IsValid())
		return false;
	// A refresh that fails for now (network, server) keeps the sign-in; only Twitch refusing it signs out.
	bool rejected = false;
	if (token_.NeedsRefresh(UnixNow()) && !RefreshToken(&rejected) && rejected)
		allowRefresh = false;

	CurlHttpClient http(&stop_);
	auto res = http.Get(twitch::kValidateUrl, {"Authorization: OAuth " + token_.accessToken});
	if (res.status == 0)
		return true; // offline; the IRC login will report a bad token
	if (!res.Ok() && res.status != 401) {
		// A server error or rate limit says nothing about the token: keep it, check again next time.
		Notice("couldn't check the sign-in (HTTP " + std::to_string(res.status) + "), keeping it");
		return true;
	}
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
	if (allowRefresh && RefreshToken(&rejected))
		return ValidateToken(false);
	if (allowRefresh && !rejected) {
		Notice("the sign-in needs refreshing and Twitch couldn't be reached for it, trying again shortly");
		return false; // kept: the next connection attempt validates again
	}

	SharedAccounts().Invalidate(Service::Twitch, token_);
	token_ = {};
	if (callbacks_.onTokenChanged)
		callbacks_.onTokenChanged(token_, login_);
	Notice("sign-in expired, reading chat anonymously. Sign in again in Tools → OBS Tools → Accounts to send "
	       "messages");
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
	CurlHttpClient http(&stop_, "Twitch chat API"); // for moderation and viewer counts, one reused connection

	for (const auto &line : session.Start()) {
		if (!SendLine(curl, sock, line))
			return;
	}

	auto lastReceive = Clock::now();
	auto lastValidate = Clock::now();
	auto nextViewerCheck = Clock::now(); // soon after joining, then every viewerCheckInterval_
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
			Moderate(action, session.ChannelId(), http);
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
				if (!out.notices.empty() || !out.eventNotices.empty() || !out.moderation.empty() ||
				    out.authFailed || out.reconnect)
					flush();
				for (auto &event : out.moderation) {
					if (callbacks_.onModeration)
						callbacks_.onModeration(std::move(event));
				}
				if (!out.channelId.empty() && callbacks_.onChannelId)
					callbacks_.onChannelId(out.channelId);
				for (const auto &notice : out.notices)
					Notice(notice);
				for (const auto &notice : out.eventNotices)
					EventNotice(notice);
				if (out.authFailed) {
					bool rejected = false;
					if (!RefreshToken(&rejected) && !rejected) {
						Notice("login failed and the sign-in couldn't be refreshed now, retrying");
					} else if (rejected) {
						SharedAccounts().Invalidate(Service::Twitch, token_);
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
		const bool canCountViewers = viewerCheckInterval_.count() > 0 && token_.IsValid() &&
					     session.IsJoined() && !session.ChannelId().empty();
		if (canCountViewers && now >= nextViewerCheck) {
			nextViewerCheck = now + viewerCheckInterval_;
			CheckViewers(session.ChannelId(), http);
		}
		if (token_.IsValid() && now - lastValidate > kValidateInterval) {
			lastValidate = now;
			if (!ValidateToken(true))
				return;
			if (EventSubReady()) {
				// E.g. OBS started offline, so the first check couldn't tell what the sign-in allows.
				Notice("moving chat to EventSub");
				return;
			}
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

static bool HasScope(const TwitchIdentity &identity, std::string_view scope)
{
	return std::find(identity.scopes.begin(), identity.scopes.end(), scope) != identity.scopes.end();
}

static std::string HttpError(const HttpResponse &res)
{
	if (res.status == 0)
		return res.error;
	const std::string reason = ModerationErrorMessage(res.body);
	return "HTTP " + std::to_string(res.status) + (reason.empty() ? "" : ", " + reason);
}

bool TwitchConnection::EventSubReady()
{
	if (!useEventSub_ || !token_.IsValid() || identity_.userId.empty())
		return false;
	for (const char *scope : twitch::kEventSubChatScopes) {
		if (!HasScope(identity_, scope)) {
			if (!scopeNoticeShown_)
				Notice("sign in to Twitch again (Tools → OBS Tools → Accounts) to move chat to EventSub; "
				       "using IRC until then");
			scopeNoticeShown_ = true;
			return false;
		}
	}
	return true;
}

std::vector<std::string> TwitchConnection::HelixHeaders() const
{
	return {"Authorization: Bearer " + token_.accessToken, "Client-Id: " + clientId_};
}

bool TwitchConnection::LookUpUsers(HttpClient &http)
{
	// One call for the channel's id and your own display name (for your sent lines until Twitch's copy arrives).
	const std::string channel = twitch::NormalizeChannel(channel_);
	HttpResponse res;
	for (int attempt = 0; attempt < 2; ++attempt) {
		res = http.Get(std::string(twitch::kUsersUrl) + "?login=" + UrlEncode(channel) +
				       "&id=" + UrlEncode(identity_.userId),
			       HelixHeaders());
		if (res.status == 401 && attempt == 0 && RefreshToken())
			continue;
		break;
	}
	if (!res.Ok()) {
		Notice("can't look up #" + channel + " (" + HttpError(res) + ")");
		return false;
	}
	for (const auto &user : twitch::ParseUsers(res.body)) {
		if (user.login == channel)
			broadcasterId_ = user.id;
		if (user.id == identity_.userId && ownName_.empty())
			ownName_ = user.displayName;
	}
	if (broadcasterId_.empty()) {
		Notice("channel #" + channel + " not found");
		return false;
	}
	if (callbacks_.onChannelId)
		callbacks_.onChannelId(broadcasterId_);
	return true;
}

bool TwitchConnection::SubscribeChat(HttpClient &http, const std::string &sessionId)
{
	// Twitch closes the socket unless something is subscribed within 10 seconds of the welcome.
	const bool banEvents = broadcasterId_ == identity_.userId && HasScope(identity_, twitch::kBanEventsScope);
	bool first = true;
	for (const auto &body :
	     twitch::BuildChatSubscriptions(sessionId, broadcasterId_, identity_.userId, banEvents)) {
		HttpResponse res;
		for (int attempt = 0; attempt < 2; ++attempt) {
			res = http.Post(eventsub::kSubscriptionsUrl, HelixHeaders(), body, "application/json");
			if (res.status == 401 && attempt == 0 && RefreshToken())
				continue;
			break;
		}
		if (!res.Ok() && res.status != 409) { // 409: already subscribed on this session
			// Messages are essential; a moderation event only costs the strike-through.
			Notice(std::string(first ? "couldn't subscribe to chat"
						 : "couldn't subscribe to a moderation event") +
			       " (" + HttpError(res) + ")");
			if (first)
				return false;
		}
		first = false;
	}
	return true;
}

void TwitchConnection::HandleNotification(const eventsub::Message &message, std::vector<ChatMessage> &batch)
{
	auto flush = [&]() {
		if (!batch.empty() && callbacks_.onMessages)
			callbacks_.onMessages(std::move(batch));
		batch.clear();
	};
	if (message.subscriptionType == "channel.chat.message") {
		auto chat = twitch::ChatMessageFromEvent(message.event, identity_.userId);
		if (!chat)
			return;
		if (chat->isSelf) {
			ownName_ = chat->author;
			ownColor_ = chat->color;
			ownBadges_ = chat->badges;
		}
		auto sent = std::find(sentIds_.begin(), sentIds_.end(), chat->id);
		if (sent == sentIds_.end()) {
			batch.push_back(std::move(*chat));
			return;
		}
		sentIds_.erase(sent);
		flush(); // keeps order: the echo was delivered before anything in this batch
		if (callbacks_.onEchoUpdate)
			callbacks_.onEchoUpdate(std::move(*chat));
		return;
	}
	if (message.subscriptionType == "channel.chat.notification") {
		// Subs, gifts, raids: notices, as IRC showed them. Each recipient of a gift bomb is left out: the gift
		// bomb's own notice says it all, and 100 lines would mean 100 relayouts.
		if (eventsub::StringAt(message.event, {"notice_type"}) == "sub_gift" &&
		    !eventsub::StringAt(message.event, {"sub_gift", "community_gift_id"}).empty())
			return;
		if (std::string text = twitch::NotificationText(message.event); !text.empty()) {
			flush();
			EventNotice(text);
		}
		return;
	}
	if (auto moderation = twitch::ModerationFromEvent(message.subscriptionType, message.event)) {
		flush();
		if (callbacks_.onModeration)
			callbacks_.onModeration(std::move(*moderation));
	}
}

void TwitchConnection::SendViaApi(HttpClient &http, const OutgoingMessage &outgoing)
{
	auto failed = [&](const std::string &why) {
		Notice("message not sent (" + why + ")");
		if (callbacks_.onSendFailed)
			callbacks_.onSendFailed(outgoing.sendId);
	};
	auto body = twitch::BuildSendChatBody(broadcasterId_, identity_.userId, outgoing.text, outgoing.replyParentId);
	if (!body) {
		if (callbacks_.onSendFailed)
			callbacks_.onSendFailed(outgoing.sendId);
		return;
	}
	HttpResponse res;
	for (int attempt = 0; attempt < 2; ++attempt) {
		res = http.Post(twitch::kSendChatUrl, HelixHeaders(), *body, "application/json");
		if (res.status == 401 && attempt == 0 && RefreshToken())
			continue;
		break;
	}
	if (!res.Ok()) {
		failed(res.status == 429 ? "sending too fast, try again in a moment" : HttpError(res));
		return;
	}
	auto result = twitch::ParseSendChatResponse(res.body);
	if (!result || !result->sent) {
		failed(result && !result->dropReason.empty() ? result->dropReason : "Twitch dropped it");
		return;
	}
	if (callbacks_.onMessages) {
		ChatMessage echo =
			twitch::MakeLocalEcho(outgoing.text, outgoing.replyTo, ownName_, login_, ownColor_, ownBadges_);
		echo.id = result->messageId;
		echo.sendId = outgoing.sendId;
		callbacks_.onMessages({std::move(echo)});
	}
	if (!result->messageId.empty()) {
		sentIds_.push_back(result->messageId);
		if (sentIds_.size() > kMaxSentIds)
			sentIds_.pop_front();
	}
}

bool TwitchConnection::RunEventSub()
{
	SetState(LinkState::Connecting);
	CurlHttpClient http(&stop_, "Twitch chat API"); // one handle, so the Helix calls reuse a connection
	if (broadcasterId_.empty() && !LookUpUsers(http))
		return false;

	auto lastValidate = Clock::now();
	auto nextViewerCheck = Clock::now();
	bool subscribed = false;
	bool revoked = false;
	std::vector<ChatMessage> batch;

	EventSubHandlers handlers;
	handlers.logName = "Twitch chat";
	handlers.subscribe = [&](const std::string &sessionId) {
		if (!SubscribeChat(http, sessionId))
			return false;
		subscribed = true;
		SetState(LinkState::Connected);
		Notice("joined #" + twitch::NormalizeChannel(channel_) + " (EventSub)");
		return true;
	};
	handlers.notification = [&](const eventsub::Message &message) {
		HandleNotification(message, batch);
	};
	handlers.revocation = [&](const eventsub::Message &message) {
		Notice("Twitch stopped sending " + message.subscriptionType + " (" + message.status + ")");
		revoked = true;
		return true;
	};
	handlers.afterRead = [&]() {
		if (!batch.empty() && callbacks_.onMessages)
			callbacks_.onMessages(std::move(batch));
		batch.clear();
	};
	const bool countViewers = viewerCheckInterval_.count() > 0;
	handlers.work = [&]() {
		std::deque<OutgoingMessage> pending;
		std::deque<ModerationAction> actions;
		{
			std::lock_guard lock(mutex_);
			pending.swap(outgoing_);
			actions.swap(moderation_);
		}
		for (const auto &action : actions)
			Moderate(action, broadcasterId_, http);
		for (const auto &outgoing : pending) {
			if (!subscribed) {
				Notice("not joined yet, message not sent");
				if (callbacks_.onSendFailed)
					callbacks_.onSendFailed(outgoing.sendId);
				continue;
			}
			SendViaApi(http, outgoing);
		}
		const auto now = Clock::now();
		if (subscribed && countViewers && now >= nextViewerCheck) {
			nextViewerCheck = now + viewerCheckInterval_;
			CheckViewers(broadcasterId_, http);
		}
		if (now - lastValidate > kValidateInterval) {
			lastValidate = now;
			if (!ValidateToken(true) || !EventSubReady())
				return false; // reconnects as whatever the sign-in now allows
		}
		return true;
	};
	handlers.nextDue = [&]() {
		auto due = lastValidate + kValidateInterval;
		if (subscribed && countViewers)
			due = (std::min)(due, nextViewerCheck);
		return due;
	};

	const EventSubResult result = unified_chat::RunEventSub(eventsub::kUrl, stop_, wakeEvent_, handlers);
	if (stop_)
		return true;
	switch (result.end) {
	case EventSubResult::End::ConnectFailed:
		Notice("connection failed: " + result.error);
		return false;
	case EventSubResult::End::SubscribeFailed:
		Notice("couldn't start chat");
		return false;
	case EventSubResult::End::TimedOut:
		Notice("connection timed out");
		break;
	case EventSubResult::End::Closed:
		Notice(subscribed ? "disconnected" : "couldn't start chat");
		break;
	default:
		break;
	}
	return result.subscribed && !revoked;
}

bool TwitchConnection::RunIrc()
{
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
	if (rc == CURLE_OK)
		RunSession(curl);
	else
		Notice(std::string("connection failed: ") + (errorBuffer[0] ? errorBuffer : curl_easy_strerror(rc)));
	curl_easy_cleanup(curl);
	return rc == CURLE_OK;
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
		auto started = Clock::now();
		bool connected = EventSubReady() ? RunEventSub() : RunIrc();

		SetState(LinkState::Disconnected);
		if (stop_)
			break;

		// A session that stayed up for a while resets the backoff.
		if (connected && Clock::now() - started > std::chrono::seconds(30))
			backoffMs = 1000;
		WaitFor(backoffMs);
		backoffMs = (std::min)(backoffMs * 2, kMaxBackoffMs);
	}
}

} // namespace unified_chat
