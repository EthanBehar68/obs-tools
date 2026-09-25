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

#include "chat-message.hpp"
#include "http-client.hpp"
#include "oauth-device.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace unified_chat::youtube {

constexpr size_t kMaxMessageLength = 200;
constexpr const char *kApiBase = "https://www.googleapis.com/youtube/v3";

struct MessagesPage {
	std::vector<ChatMessage> messages;
	std::string nextPageToken;
	int pollingIntervalMs = 0;
	bool chatEnded = false;
};

struct OwnChannel {
	std::string id;
	std::string title;
};

// Messages whose author channel ID equals ownChannelId are marked isSelf.
std::optional<MessagesPage> ParseMessagesPage(const std::string &json, std::string_view ownChannelId = {});
std::optional<ChatMessage> ParseChatMessage(const std::string &json);
// The signed-in channel, from channels.list?mine=true.
std::optional<OwnChannel> ParseOwnChannel(const std::string &json);
std::optional<std::string> ParseBroadcastLiveChatId(const std::string &json);
std::optional<std::string> ParseVideoLiveChatId(const std::string &json);
std::string ParseErrorReason(const std::string &json);
std::string BuildInsertBody(const std::string &liveChatId, std::string_view text);

// Accepts a bare 11 character video ID or a watch, youtu.be, /live/, /shorts/ or Studio URL.
std::string ExtractVideoId(std::string_view input);

// Remembers the most recent message IDs so repeated pages never show a message twice. Stores 64-bit
// hashes rather than the ~70 character IDs; 1000 entries cover five full pages.
class RecentIds {
public:
	explicit RecentIds(size_t capacity = 1000) : capacity_(capacity) {}

	// Returns false when the ID was already seen.
	bool Insert(std::string_view id);
	size_t Size() const { return order_.size(); }

private:
	size_t capacity_;
	std::deque<uint64_t> order_;
	std::unordered_set<uint64_t> ids_;
};

enum class State { SignedOut, WaitingForBroadcast, Polling, Error };

struct StepResult {
	std::vector<ChatMessage> messages;
	std::vector<std::string> notices;
	int nextDelayMs = 0;
};

struct SendResult {
	bool ok = false;
	std::optional<ChatMessage> echo;
	std::string error;
};

// One YouTube live chat connection. All methods are called from a single worker thread;
// time is passed in so tests control it.
class ChatSession {
public:
	using TokenChanged = std::function<void(const oauth::Token &)>;

	ChatSession(HttpClient &http, oauth::Provider provider, oauth::Token token, std::string videoId, int minPollMs,
		    TokenChanged onTokenChanged);

	StepResult Step(int64_t now);
	SendResult Send(std::string_view text, int64_t now);

	State GetState() const { return state_; }
	bool CanSend() const { return state_ == State::Polling && !liveChatId_.empty(); }
	const std::string &LiveChatId() const { return liveChatId_; }

private:
	HttpResponse Authorized(bool post, const std::string &url, const std::string &body, int64_t now);
	bool Refresh(int64_t now);
	void FindLiveChat(int64_t now, StepResult &out);
	void Poll(int64_t now, StepResult &out);
	int HandleError(const HttpResponse &res, StepResult &out);

	HttpClient &http_;
	oauth::Provider provider_;
	oauth::Token token_;
	std::string videoId_;
	int minPollMs_;
	TokenChanged onTokenChanged_;

	State state_ = State::SignedOut;
	std::string liveChatId_;
	std::string pageToken_;
	std::string ownChannelId_;
	std::string ownName_;
	RecentIds seen_;
	bool announcedWaiting_ = false;
};

} // namespace unified_chat::youtube
