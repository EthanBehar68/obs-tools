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

#include "core/obs-tools.hpp"

#include "core/accounts.hpp"
#include "core/chat-message.hpp"
#include "core/http-client.hpp"
#include "core/oauth-device.hpp"
#include "core/stream-info.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace unified_chat {

// The stream info window's requests, on a thread of its own that exists only while the window is open. Uses the
// shared sign-ins: the Twitch one edits the signed-in user's own channel. Callbacks run on the worker thread.
class StreamInfoClient {
public:
	struct TwitchState {
		enum class Status { SignedOut, NeedsScope, Ready, Error };
		Status status = Status::SignedOut;
		std::string login;
		TwitchChannelInfo info;
		std::string error;
	};
	struct YouTubeState {
		enum class Status { SignedOut, NoBroadcast, Ready, Error };
		Status status = Status::SignedOut;
		YouTubeBroadcast broadcast;
		YouTubeVideoSnippet snippet;
		std::string error;
	};
	struct Callbacks {
		std::function<void(TwitchState)> onTwitch;
		std::function<void(YouTubeState)> onYouTube;
		std::function<void(std::string query, std::vector<TwitchCategory>)> onCategories;
		std::function<void(std::string url, std::string bytes)> onImage;
		// After Apply, once per platform asked to change: empty error on success.
		std::function<void(Platform, std::string error)> onApplied;
	};
	struct Update {
		std::optional<TwitchChannelUpdate> twitch;
		bool youtube = false;
		std::string youtubeVideoId; // the broadcast Load found
		std::optional<std::string> youtubeTitle;
		std::optional<std::string> youtubeCategoryId;
	};

	explicit StreamInfoClient(Callbacks callbacks);
	~StreamInfoClient();

	StreamInfoClient(const StreamInfoClient &) = delete;
	StreamInfoClient &operator=(const StreamInfoClient &) = delete;

	// Reads the current title, category and tags (YouTube: 2 or 3 quota units). Apply: YouTube 51.
	void Load(bool twitch, bool youtube);
	// Only the latest search waiting is run.
	void Search(std::string query);
	void FetchImage(std::string url);
	void Apply(Update update);

private:
	void Run();
	void LoadTwitch(HttpClient &http);
	void LoadYouTube(HttpClient &http);
	void RunSearch(HttpClient &http, const std::string &query);
	void RunApply(HttpClient &http, const Update &update);
	std::string ApplyYouTube(HttpClient &http, const Update &update);
	// The signed-in user's token, user id and scopes, refreshed if Twitch says it expired.
	bool ReadyTwitch(HttpClient &http, TwitchState &state);
	bool ReadyGoogle();
	// A Helix call, refreshing the token once on 401.
	HttpResponse Helix(HttpClient &http, const char *method, const std::string &url, const std::string &body = {});
	// A Data API call, refreshing the token once on 401.
	HttpResponse Google(HttpClient &http, const char *method, const std::string &url, const std::string &body = {});
	bool Refresh(Service service);

	Callbacks callbacks_;
	std::atomic<bool> stop_ = false;
	std::mutex mutex_;
	std::condition_variable cv_;
	bool loadTwitch_ = false, loadYouTube_ = false;
	std::optional<std::string> search_;
	std::deque<std::string> images_;
	std::deque<Update> applies_;

	// Worker thread only.
	std::string twitchClientId_;
	oauth::Token twitchToken_;
	std::string twitchUserId_;
	oauth::Token googleToken_;
	std::thread thread_;
};

} // namespace unified_chat
