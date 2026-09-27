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

#pragma once

#include "core/chat-message.hpp"
#include "core/moderation.hpp"
#include "core/oauth-device.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace unified_chat {

// Standby: deliberately not connected yet (YouTube waits for OBS to start streaming).
enum class LinkState { Disconnected, Standby, Connecting, ReadOnly, Connected };

struct ConnectionCallbacks {
	// Messages that arrived together (one socket read or one poll), in order.
	std::function<void(std::vector<ChatMessage>)> onMessages;
	// A deletion, timeout, ban or chat clear; always after the messages that arrived before it.
	std::function<void(ModerationEvent)> onModeration;
	// Twitch: the channel's numeric id became known (for emote services and badges).
	std::function<void(std::string channelId)> onChannelId;
	std::function<void(const std::string &)> onNotice;
	std::function<void(LinkState)> onState;
	std::function<void(const oauth::Token &, const std::string &login)> onTokenChanged;
	// A message queued with Send() could not be delivered (the reason is reported via onNotice).
	std::function<void(uint64_t sendId)> onSendFailed;
};

struct OutgoingMessage {
	std::string text;
	uint64_t sendId = 0;
	std::string replyParentId; // Twitch only: id of the message this answers
	std::string replyTo;       // its author's display name, for the local echo
};

// Owns a worker thread holding one Twitch IRC connection (TLS via libcurl CONNECT_ONLY).
// Callbacks run on the worker thread.
class TwitchConnection {
public:
	TwitchConnection(std::string channel, std::string clientId, std::string login, oauth::Token token,
			 ConnectionCallbacks callbacks);
	~TwitchConnection();

	TwitchConnection(const TwitchConnection &) = delete;
	TwitchConnection &operator=(const TwitchConnection &) = delete;

	// sendId is copied onto the local echo so the dock can match it up. With replyParentId the message is sent
	// as a threaded reply to that message.
	void Send(std::string text, uint64_t sendId = 0, std::string replyParentId = {}, std::string replyTo = {});
	// Delete / timeout / ban / unban from the dock (Helix). Results arrive through chat events and notices.
	void Moderate(ModerationAction action);

private:
	void Run();
	bool ValidateToken(bool allowRefresh);
	bool RefreshToken();
	void RunSession(void *curl);
	void WaitFor(int ms);
	void Wake();
	void Moderate(const ModerationAction &action, const std::string &channelId);
	void Notice(const std::string &text);
	void SetState(LinkState state);

	std::string channel_;
	std::string clientId_;
	std::string login_;
	oauth::Token token_;
	ConnectionCallbacks callbacks_;

	std::atomic<bool> stop_ = false;
	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<OutgoingMessage> outgoing_;
	std::deque<ModerationAction> moderation_;
	TwitchIdentity identity_;   // from the token check (worker thread only): user id and granted scopes
	void *wakeEvent_ = nullptr; // Windows auto-reset event, set by Send() and on shutdown
	std::thread thread_;
};

} // namespace unified_chat
