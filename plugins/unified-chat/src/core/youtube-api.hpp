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
#include "core/http-client.hpp"
#include "moderation.hpp"
#include "core/oauth-device.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace unified_chat::youtube {

constexpr size_t kMaxMessageLength = 200;
constexpr const char *kApiBase = "https://www.googleapis.com/youtube/v3";

struct MessagesPage {
	std::vector<ChatMessage> messages;
	std::vector<ModerationEvent> moderation; // bans
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
std::optional<std::string> ParseBroadcastVideoId(const std::string &json);
// videos.list liveStreamingDetails.concurrentViewers; nullopt when absent (e.g. hidden or not live).
std::optional<int64_t> ParseConcurrentViewers(const std::string &json);
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
	std::vector<ModerationEvent> moderation;
	std::optional<int64_t> viewers; // set when checked: the concurrent viewer count, or -1 when not available
	std::vector<std::string> notices;
	int nextDelayMs = 0;
};

struct SendResult {
	bool ok = false;
	std::optional<ChatMessage> echo;
	std::string error;
};

struct ModerationOutcome {
	bool ok = false;
	std::string error;
	std::optional<ModerationEvent> strike; // a deleted message the view should mark (YouTube doesn't report it)
	std::string notice;                    // e.g. "YouTube: x was unbanned"
};

// One YouTube live chat connection. All methods are called from a single worker thread;
// time is passed in so tests control it.
class ChatSession {
public:
	using TokenChanged = std::function<void(const oauth::Token &)>;
	using MessageSink = std::function<void(std::vector<ChatMessage>)>;

	ChatSession(HttpClient &http, oauth::Provider provider, oauth::Token token, std::string videoId, int minPollMs,
		    TokenChanged onTokenChanged);

	// Streaming (liveChatMessages.streamList): each Step holds one server-pushed stream open (the server ends
	// it after about 10 s) and hands messages to the live sink as they arrive. Unexpected failures fall back to
	// polling until the next chat. Off by default.
	void SetStreaming(bool enabled) { streaming_ = enabled; }
	// Receives streamed messages while a Step is still running. Without a sink they go into StepResult.
	void SetLiveSink(MessageSink sink) { liveSink_ = std::move(sink); }
	// Receives bans from a stream while a Step is running, after the messages that arrived with them. When
	// polling, or without a live sink, bans go into StepResult behind its messages.
	void SetModerationSink(std::function<void(ModerationEvent)> sink) { moderationSink_ = std::move(sink); }
	// Checked while a stream is open; returning true ends the Step early (e.g. to send a message).
	void SetInterrupt(std::function<bool()> interrupt) { interrupt_ = std::move(interrupt); }
	bool IsStreaming() const { return streaming_ && !streamFailed_; }
	// Check the live viewer count every 5 minutes while connected (1 quota unit each). Off by default.
	void SetViewerChecks(bool enabled) { viewerChecks_ = enabled; }
	// Messages posted before this (unix seconds) are chat history, e.g. from an earlier session on a reused
	// broadcast, and aren't shown. Messages without a timestamp are always shown. 0 = show everything.
	void SetHistoryCutoff(int64_t unixSeconds) { historyCutoff_ = unixSeconds; }
	// How often to look for a live broadcast while none is found: every fastMs until fastUntil (unix seconds,
	// e.g. just after the stream started, while YouTube is still bringing the broadcast up), then every normalMs.
	void SetBroadcastSearch(int normalMs, int fastMs = 0, int64_t fastUntil = 0)
	{
		searchMs_ = normalMs;
		fastSearchMs_ = fastMs;
		fastSearchUntil_ = fastUntil;
	}

	StepResult Step(int64_t now);
	SendResult Send(std::string_view text, int64_t now);
	// Deletes a message, times out, bans or (for bans made here) unbans. Needs the live chat.
	ModerationOutcome Moderate(const ModerationAction &action, int64_t now);

	State GetState() const { return state_; }
	bool CanSend() const { return state_ == State::Polling && !liveChatId_.empty(); }
	const std::string &LiveChatId() const { return liveChatId_; }

private:
	HttpResponse Authorized(bool post, const std::string &url, const std::string &body, int64_t now);
	HttpResponse Authorized(const HttpRequest &request, int64_t now);
	bool Refresh(int64_t now);
	void FindLiveChat(int64_t now, StepResult &out);
	void Poll(int64_t now, StepResult &out);
	void Stream(int64_t now, StepResult &out);
	void CheckViewers(int64_t now, StepResult &out);
	void FallBackToPolling(const std::string &reason, StepResult &out);
	void EndChat(StepResult &out);
	void Deliver(std::vector<ChatMessage> &messages, StepResult &out);
	void DeliverModeration(std::vector<ModerationEvent> &events, StepResult &out, bool live);
	bool IsHistory(const ChatMessage &chat) const
	{
		return historyCutoff_ != 0 && chat.postedAt != 0 && chat.postedAt < historyCutoff_;
	}
	int HandleError(const HttpResponse &res, StepResult &out);

	HttpClient &http_;
	oauth::Provider provider_;
	oauth::Token token_;
	std::string videoId_;
	int minPollMs_;
	TokenChanged onTokenChanged_;

	State state_ = State::SignedOut;
	std::string liveChatId_;
	std::string liveVideoId_; // the broadcast's video, for the viewer count
	int64_t nextViewerCheck_ = 0;
	bool viewerChecks_ = false;
	std::string pageToken_;
	std::string ownChannelId_;
	std::string ownName_;
	RecentIds seen_;
	bool announcedWaiting_ = false;

	int64_t historyCutoff_ = 0;
	int searchMs_ = 30000;
	int fastSearchMs_ = 0;
	int64_t fastSearchUntil_ = 0;

	bool streaming_ = false;
	bool streamFailed_ = false;
	MessageSink liveSink_;
	std::function<void(ModerationEvent)> moderationSink_;
	std::unordered_map<std::string, std::string> bans_; // banned channel id -> ban id, for bans made here
	std::function<bool()> interrupt_;
};

} // namespace unified_chat::youtube
