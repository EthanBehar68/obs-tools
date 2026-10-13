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

#include "stream-info-client.hpp"
#include "net/account-store.hpp"
#include "net/curl-http-client.hpp"
#include "core/moderation.hpp"

#include <algorithm>
#include <ctime>

namespace unified_chat {

static std::string Failure(const HttpResponse &res)
{
	if (res.status == 0)
		return res.error;
	const std::string reason = StreamInfoErrorMessage(res.body);
	return "HTTP " + std::to_string(res.status) + (reason.empty() ? "" : ": " + reason);
}

StreamInfoClient::StreamInfoClient(Callbacks callbacks) : callbacks_(std::move(callbacks))
{
	thread_ = std::thread(&StreamInfoClient::Run, this);
}

StreamInfoClient::~StreamInfoClient()
{
	stop_ = true;
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

void StreamInfoClient::Load(bool twitch, bool youtube)
{
	{
		std::lock_guard lock(mutex_);
		loadTwitch_ = loadTwitch_ || twitch;
		loadYouTube_ = loadYouTube_ || youtube;
	}
	cv_.notify_all();
}

void StreamInfoClient::Search(std::string query)
{
	{
		std::lock_guard lock(mutex_);
		search_ = std::move(query);
	}
	cv_.notify_all();
}

void StreamInfoClient::FetchImage(std::string url)
{
	{
		std::lock_guard lock(mutex_);
		images_.push_back(std::move(url));
	}
	cv_.notify_all();
}

void StreamInfoClient::Apply(Update update)
{
	{
		std::lock_guard lock(mutex_);
		applies_.push_back(std::move(update));
	}
	cv_.notify_all();
}

void StreamInfoClient::Run()
{
	CurlHttpClient http(&stop_, "Stream info"); // Helix and the Data API; one connection each, reused
	CurlHttpClient images(&stop_);
	while (!stop_) {
		bool twitch = false, youtube = false;
		std::optional<std::string> query;
		std::optional<Update> update;
		std::optional<std::string> image;
		{
			std::unique_lock lock(mutex_);
			cv_.wait(lock, [this] {
				return stop_ || loadTwitch_ || loadYouTube_ || search_ || !images_.empty() ||
				       !applies_.empty();
			});
			if (stop_)
				break;
			// Saving first, then what the window shows; images last, a search can make them stale.
			if (!applies_.empty()) {
				update = std::move(applies_.front());
				applies_.pop_front();
			} else if (loadTwitch_ || loadYouTube_) {
				std::swap(twitch, loadTwitch_);
				std::swap(youtube, loadYouTube_);
			} else if (search_) {
				query = std::move(search_);
				search_.reset();
			} else {
				image = std::move(images_.front());
				images_.pop_front();
			}
		}
		if (update)
			RunApply(http, *update);
		if (twitch)
			LoadTwitch(http);
		if (youtube)
			LoadYouTube(http);
		if (query)
			RunSearch(http, *query);
		if (image) {
			auto res = images.Get(*image, {});
			if (res.Ok() && callbacks_.onImage)
				callbacks_.onImage(*image, std::move(res.body));
		}
	}
}

bool StreamInfoClient::Refresh(Service service)
{
	oauth::Token &token = service == Service::Twitch ? twitchToken_ : googleToken_;
	// Through the shared store: Twitch refresh tokens work once, and another plugin may have spent this one.
	auto fresh = SharedAccounts().Refresh(service, token, &stop_);
	if (!fresh.Granted())
		return false;
	token = fresh.token;
	return true;
}

bool StreamInfoClient::ReadyTwitch(HttpClient &http, TwitchState &state)
{
	const Account account = SharedAccounts().Load(nullptr, &stop_).twitch;
	twitchClientId_ = account.clientId;
	twitchToken_ = account.token;
	state.login = account.login;
	if (!twitchToken_.IsValid() || twitchClientId_.empty()) {
		state.status = TwitchState::Status::SignedOut;
		return false;
	}
	if (twitchToken_.NeedsRefresh((int64_t)std::time(nullptr)))
		Refresh(Service::Twitch);

	auto validate = [&]() {
		return http.Get(oauth::kTwitchValidateUrl, {"Authorization: OAuth " + twitchToken_.accessToken});
	};
	HttpResponse res = validate();
	if (oauth::ClassifyValidate(res.status) == oauth::TokenCheck::Expired && Refresh(Service::Twitch))
		res = validate();
	switch (oauth::ClassifyValidate(res.status)) {
	case oauth::TokenCheck::Expired:
		// Signing out is left to the chat connection, which also sees it.
		state.status = TwitchState::Status::SignedOut;
		return false;
	case oauth::TokenCheck::Unknown:
		state.status = TwitchState::Status::Error;
		state.error = Failure(res);
		return false;
	case oauth::TokenCheck::Valid:
		break;
	}
	const auto identity = ParseTwitchIdentity(res.body);
	if (!identity || identity->userId.empty()) {
		state.status = TwitchState::Status::Error;
		state.error = "unexpected answer from Twitch";
		return false;
	}
	twitchUserId_ = identity->userId;
	if (!identity->login.empty())
		state.login = identity->login;
	const bool allowed = std::find(identity->scopes.begin(), identity->scopes.end(), kStreamInfoScope) !=
			     identity->scopes.end();
	state.status = allowed ? TwitchState::Status::Ready : TwitchState::Status::NeedsScope;
	return true;
}

HttpResponse StreamInfoClient::Helix(HttpClient &http, const char *method, const std::string &url,
				     const std::string &body)
{
	auto perform = [&]() {
		const std::vector<std::string> headers{"Authorization: Bearer " + twitchToken_.accessToken,
						       "Client-Id: " + twitchClientId_};
		return std::string_view(method) == "GET" ? http.Get(url, headers)
							 : http.Send(method, url, headers, body, "application/json");
	};
	HttpResponse res = perform();
	if (res.status == 401 && Refresh(Service::Twitch))
		res = perform();
	return res;
}

bool StreamInfoClient::ReadyGoogle()
{
	googleToken_ = SharedAccounts().Load(nullptr, &stop_).google.token;
	if (!googleToken_.IsValid())
		return false;
	if (googleToken_.NeedsRefresh((int64_t)std::time(nullptr)))
		Refresh(Service::Google);
	return true;
}

HttpResponse StreamInfoClient::Google(HttpClient &http, const char *method, const std::string &url,
				      const std::string &body)
{
	auto perform = [&]() {
		const std::vector<std::string> headers{"Authorization: Bearer " + googleToken_.accessToken};
		return std::string_view(method) == "GET" ? http.Get(url, headers)
							 : http.Send(method, url, headers, body, "application/json");
	};
	HttpResponse res = perform();
	if (res.status == 401 && Refresh(Service::Google))
		res = perform();
	return res;
}

void StreamInfoClient::LoadTwitch(HttpClient &http)
{
	TwitchState state;
	if (ReadyTwitch(http, state)) {
		// Reading needs no scope, so even before signing in again the window shows what's set.
		const HttpResponse res = Helix(http, "GET", TwitchChannelUrl(twitchUserId_));
		auto info = res.Ok() ? ParseTwitchChannelInfo(res.body) : std::nullopt;
		if (info) {
			state.info = std::move(*info);
		} else {
			state.status = TwitchState::Status::Error;
			state.error = res.Ok() ? "unexpected answer from Twitch" : Failure(res);
		}
	}
	if (callbacks_.onTwitch)
		callbacks_.onTwitch(std::move(state));
}

void StreamInfoClient::LoadYouTube(HttpClient &http)
{
	YouTubeState state;
	if (!ReadyGoogle()) {
		if (callbacks_.onYouTube)
			callbacks_.onYouTube(std::move(state));
		return;
	}
	// The live broadcast, else the next one: the title can be set before going live.
	for (bool active : {true, false}) {
		const HttpResponse res = Google(http, "GET", YouTubeBroadcastsUrl(active));
		if (!res.Ok()) {
			state.status = YouTubeState::Status::Error;
			state.error = Failure(res);
			break;
		}
		if (auto broadcast = PickYouTubeBroadcast(res.body, active)) {
			state.broadcast = std::move(*broadcast);
			break;
		}
	}
	if (state.status != YouTubeState::Status::Error) {
		if (state.broadcast.id.empty()) {
			state.status = YouTubeState::Status::NoBroadcast;
		} else {
			const HttpResponse res = Google(http, "GET", YouTubeVideoUrl(state.broadcast.id));
			auto snippet = res.Ok() ? ParseYouTubeVideoSnippet(res.body) : std::nullopt;
			if (snippet) {
				state.snippet = std::move(*snippet);
				state.status = YouTubeState::Status::Ready;
			} else {
				state.status = YouTubeState::Status::Error;
				state.error = res.Ok() ? "the broadcast's video wasn't found" : Failure(res);
			}
		}
	}
	if (callbacks_.onYouTube)
		callbacks_.onYouTube(std::move(state));
}

void StreamInfoClient::RunSearch(HttpClient &http, const std::string &query)
{
	std::vector<TwitchCategory> categories;
	if (twitchToken_.IsValid() && !query.empty()) {
		const HttpResponse res = Helix(http, "GET", TwitchCategorySearchUrl(query));
		if (res.Ok())
			categories = ParseTwitchCategories(res.body);
	}
	if (callbacks_.onCategories)
		callbacks_.onCategories(query, std::move(categories));
}

void StreamInfoClient::RunApply(HttpClient &http, const Update &update)
{
	if (update.twitch) {
		std::string error;
		if (!twitchToken_.IsValid() || twitchUserId_.empty()) {
			error = "not signed in";
		} else {
			const HttpResponse res = Helix(http, "PATCH", TwitchChannelUrl(twitchUserId_),
						       BuildTwitchChannelPatch(*update.twitch));
			if (!res.Ok())
				error = res.status == 401 ? "sign in to Twitch again to allow changing stream info"
							  : Failure(res);
		}
		if (callbacks_.onApplied)
			callbacks_.onApplied(Platform::Twitch, error);
	}
	if (update.youtube) {
		const std::string error = ApplyYouTube(http, update);
		if (callbacks_.onApplied)
			callbacks_.onApplied(Platform::YouTube, error);
	}
}

std::string StreamInfoClient::ApplyYouTube(HttpClient &http, const Update &update)
{
	if (!googleToken_.IsValid())
		return "not signed in";
	const std::string &videoId = update.youtubeVideoId;
	if (videoId.empty())
		return "no live or upcoming broadcast";
	// Read again just before writing (1 unit): a description changed in YouTube Studio since is kept.
	HttpResponse res = Google(http, "GET", YouTubeVideoUrl(videoId));
	auto current = res.Ok() ? ParseYouTubeVideoSnippet(res.body) : std::nullopt;
	if (!current)
		return res.Ok() ? "the broadcast's video wasn't found" : Failure(res);
	const auto body = BuildYouTubeVideoUpdate(videoId, *current, update.youtubeTitle, update.youtubeCategoryId);
	if (!body)
		return "the video needs a title and a category";
	res = Google(http, "PUT", YouTubeVideoUpdateUrl(), *body);
	return res.Ok() ? std::string() : Failure(res);
}

} // namespace unified_chat
