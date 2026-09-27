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

#include "twitch-connection.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace unified_chat {

// Owns a worker thread that streams (or polls) one YouTube live chat and posts outgoing messages.
// Callbacks run on the worker thread.
class YouTubeConnection {
public:
	YouTubeConnection(std::string clientId, std::string clientSecret, oauth::Token token, std::string video,
			  int pollSeconds, bool stream, bool startedWithStream, int64_t historyCutoff,
			  ConnectionCallbacks callbacks);
	~YouTubeConnection();

	YouTubeConnection(const YouTubeConnection &) = delete;
	YouTubeConnection &operator=(const YouTubeConnection &) = delete;

	// sendId is copied onto the local echo so the dock can match it up.
	void Send(std::string text, uint64_t sendId = 0);
	// Delete / timeout / ban / unban from the dock; the outcome arrives as notices and moderation events.
	void Moderate(ModerationAction action);

private:
	void Run();

	std::string clientId_;
	std::string clientSecret_;
	oauth::Token token_;
	std::string video_;
	int pollSeconds_;
	bool stream_;
	bool startedWithStream_;
	int64_t historyCutoff_;
	ConnectionCallbacks callbacks_;

	std::atomic<bool> stop_ = false;
	std::atomic<bool> sendPending_ = false;
	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<OutgoingMessage> outgoing_;
	std::deque<ModerationAction> moderation_;
	std::thread thread_;
};

} // namespace unified_chat
