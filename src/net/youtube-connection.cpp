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

#include "youtube-connection.hpp"
#include "curl-http-client.hpp"
#include "core/youtube-api.hpp"

#include <chrono>
#include <ctime>

namespace unified_chat {

using Clock = std::chrono::steady_clock;

static LinkState ToLinkState(youtube::State state)
{
	switch (state) {
	case youtube::State::Polling:
		return LinkState::Connected;
	case youtube::State::WaitingForBroadcast:
		return LinkState::Connecting;
	default:
		return LinkState::Disconnected;
	}
}

YouTubeConnection::YouTubeConnection(std::string clientId, std::string clientSecret, oauth::Token token,
				     std::string video, int pollSeconds, ConnectionCallbacks callbacks)
	: clientId_(std::move(clientId)),
	  clientSecret_(std::move(clientSecret)),
	  token_(std::move(token)),
	  video_(std::move(video)),
	  pollSeconds_(pollSeconds),
	  callbacks_(std::move(callbacks))
{
	thread_ = std::thread(&YouTubeConnection::Run, this);
}

YouTubeConnection::~YouTubeConnection()
{
	stop_ = true;
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

void YouTubeConnection::Send(std::string text, uint64_t sendId)
{
	{
		std::lock_guard lock(mutex_);
		outgoing_.push_back({std::move(text), sendId});
	}
	cv_.notify_all();
}

void YouTubeConnection::Run()
{
	auto notice = [this](const std::string &text) {
		if (callbacks_.onNotice)
			callbacks_.onNotice(text);
	};

	if (!token_.IsValid()) {
		notice("YouTube: not signed in, open the chat settings to sign in");
		if (callbacks_.onState)
			callbacks_.onState(LinkState::Disconnected);
		return;
	}

	CurlHttpClient http(&stop_);
	youtube::ChatSession session(http, oauth::GoogleProvider(clientId_, clientSecret_), token_, video_,
				     pollSeconds_ * 1000, [this](const oauth::Token &token) {
					     if (callbacks_.onTokenChanged)
						     callbacks_.onTokenChanged(token, {});
				     });

	LinkState reported = LinkState::Disconnected;
	auto nextStep = Clock::now();

	while (!stop_) {
		std::deque<OutgoingMessage> pending;
		{
			std::unique_lock lock(mutex_);
			cv_.wait_until(lock, nextStep, [this] { return stop_.load() || !outgoing_.empty(); });
			pending.swap(outgoing_);
		}
		if (stop_)
			break;

		for (const auto &outgoing : pending) {
			auto result = session.Send(outgoing.text, (int64_t)std::time(nullptr));
			if (result.echo) {
				result.echo->sendId = outgoing.sendId;
				if (callbacks_.onMessage)
					callbacks_.onMessage(*result.echo);
				continue;
			}
			if (!result.ok)
				notice("YouTube: message not sent (" + result.error + ")");
			// Sent without an echo in the response: the next poll will show it instead.
			if (callbacks_.onSendFailed)
				callbacks_.onSendFailed(outgoing.sendId);
		}

		if (Clock::now() >= nextStep) {
			auto step = session.Step((int64_t)std::time(nullptr));
			for (const auto &message : step.messages) {
				if (callbacks_.onMessage)
					callbacks_.onMessage(message);
			}
			for (const auto &text : step.notices)
				notice(text);
			nextStep = Clock::now() + std::chrono::milliseconds(step.nextDelayMs);
		}

		LinkState state = ToLinkState(session.GetState());
		if (state != reported) {
			reported = state;
			if (callbacks_.onState)
				callbacks_.onState(state);
		}
	}
}

} // namespace unified_chat
