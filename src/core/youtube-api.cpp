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

#include "youtube-api.hpp"
#include "json-array-reader.hpp"
#include "text-util.hpp"

#include <algorithm>
#include <iterator>
#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat::youtube {

static constexpr int kNetworkRetryMs = 10000;
static constexpr int kErrorRetryMs = 60000;
static constexpr int kQuotaRetryMs = 15 * 60 * 1000;

// Partial responses: only the properties the parsers below read, to cut download and JSON parsing.
// Keep these in step with MessageFromItem, ParseMessagesPage and the First*/ParseOwnChannel readers.
static constexpr const char *kMessagesFields =
	"&fields=nextPageToken,pollingIntervalMillis,offlineAt,"
	"items(id,snippet(type,publishedAt,displayMessage,textMessageDetails/messageText),"
	"authorDetails(displayName,channelId,isChatOwner,isChatModerator,isChatSponsor))";
static constexpr const char *kBroadcastFields = "&fields=items/snippet/liveChatId";
static constexpr const char *kVideoFields = "&fields=items/liveStreamingDetails/activeLiveChatId";
static constexpr const char *kChannelFields = "&fields=items(id,snippet/title)";

static std::string StringAt(const json &obj, std::initializer_list<const char *> path)
{
	const json *node = &obj;
	for (const char *key : path) {
		if (!node->is_object())
			return {};
		auto it = node->find(key);
		if (it == node->end())
			return {};
		node = &*it;
	}
	return node->is_string() ? node->get<std::string>() : std::string();
}

static bool BoolAt(const json &obj, const char *section, const char *key)
{
	auto it = obj.find(section);
	if (it == obj.end() || !it->is_object())
		return false;
	auto value = it->find(key);
	return value != it->end() && value->is_boolean() && value->get<bool>();
}

static std::optional<ChatMessage> MessageFromItem(const json &item, std::string_view ownChannelId)
{
	if (!item.is_object())
		return std::nullopt;

	ChatMessage chat;
	chat.platform = Platform::YouTube;
	chat.id = StringAt(item, {"id"});
	chat.author = StringAt(item, {"authorDetails", "displayName"});
	// Live chat shows @handles (since late 2025) and displayName appears to carry them; a mention is "@handle".
	chat.mention = chat.author.rfind('@', 0) == 0 ? chat.author.substr(1) : chat.author;
	chat.text = StringAt(item, {"snippet", "displayMessage"});
	if (chat.text.empty())
		chat.text = StringAt(item, {"snippet", "textMessageDetails", "messageText"});
	if (chat.text.empty())
		return std::nullopt;

	if (BoolAt(item, "authorDetails", "isChatOwner"))
		chat.color = "#ffd600";
	else if (BoolAt(item, "authorDetails", "isChatModerator"))
		chat.color = "#5e84f1";
	else if (BoolAt(item, "authorDetails", "isChatSponsor"))
		chat.color = "#2ba640";
	chat.isSelf = !ownChannelId.empty() && StringAt(item, {"authorDetails", "channelId"}) == ownChannelId;
	chat.postedAt = ParseRfc3339(StringAt(item, {"snippet", "publishedAt"}));
	return chat;
}

std::optional<MessagesPage> ParseMessagesPage(const std::string &body, std::string_view ownChannelId)
{
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;

	MessagesPage page;
	page.nextPageToken = StringAt(obj, {"nextPageToken"});
	auto interval = obj.find("pollingIntervalMillis");
	if (interval != obj.end() && interval->is_number_integer())
		page.pollingIntervalMs = interval->get<int>();
	page.chatEnded = obj.contains("offlineAt");

	auto items = obj.find("items");
	if (items != obj.end() && items->is_array()) {
		for (const auto &item : *items) {
			if (StringAt(item, {"snippet", "type"}) == "chatEndedEvent") {
				page.chatEnded = true;
				continue;
			}
			if (auto chat = MessageFromItem(item, ownChannelId))
				page.messages.push_back(std::move(*chat));
		}
	}
	return page;
}

std::optional<ChatMessage> ParseChatMessage(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	return MessageFromItem(obj, {});
}

static std::optional<std::string> FirstItemString(const std::string &body, std::initializer_list<const char *> path)
{
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;
	auto items = obj.find("items");
	if (items == obj.end() || !items->is_array())
		return std::nullopt;
	for (const auto &item : *items) {
		std::string value = StringAt(item, path);
		if (!value.empty())
			return value;
	}
	return std::nullopt;
}

std::optional<std::string> ParseBroadcastLiveChatId(const std::string &body)
{
	return FirstItemString(body, {"snippet", "liveChatId"});
}

std::optional<std::string> ParseVideoLiveChatId(const std::string &body)
{
	return FirstItemString(body, {"liveStreamingDetails", "activeLiveChatId"});
}

std::optional<OwnChannel> ParseOwnChannel(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;
	auto items = obj.find("items");
	if (items == obj.end() || !items->is_array() || items->empty())
		return std::nullopt;
	const json &item = items->front();
	return OwnChannel{StringAt(item, {"id"}), StringAt(item, {"snippet", "title"})};
}

std::string ParseErrorReason(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return {};
	auto error = obj.find("error");
	if (error == obj.end())
		return {};
	if (error->is_string())
		return error->get<std::string>();
	if (!error->is_object())
		return {};
	auto errors = error->find("errors");
	if (errors != error->end() && errors->is_array() && !errors->empty()) {
		std::string reason = StringAt((*errors)[0], {"reason"});
		if (!reason.empty())
			return reason;
	}
	return StringAt(*error, {"message"});
}

std::string BuildInsertBody(const std::string &liveChatId, std::string_view text)
{
	json body = {{"snippet",
		      {{"liveChatId", liveChatId},
		       {"type", "textMessageEvent"},
		       {"textMessageDetails", {{"messageText", Trim(StripLineBreaks(text))}}}}}};
	return body.dump();
}

static bool IsVideoIdChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static bool IsVideoId(std::string_view value)
{
	return value.size() == 11 && std::all_of(value.begin(), value.end(), IsVideoIdChar);
}

std::string ExtractVideoId(std::string_view input)
{
	std::string value = Trim(input);
	if (IsVideoId(value))
		return value;

	std::string candidate;
	auto v = value.find("v=");
	if (v != std::string::npos && (v == 0 || value[v - 1] == '?' || value[v - 1] == '&')) {
		candidate = value.substr(v + 2);
	} else {
		for (const char *marker : {"youtu.be/", "/live/", "/shorts/", "/embed/", "/video/"}) {
			auto pos = value.find(marker);
			if (pos != std::string::npos) {
				candidate = value.substr(pos + std::char_traits<char>::length(marker));
				break;
			}
		}
	}
	auto end = candidate.find_first_of("?&#/");
	if (end != std::string::npos)
		candidate.resize(end);
	return IsVideoId(candidate) ? candidate : std::string();
}

bool RecentIds::Insert(std::string_view id)
{
	if (id.empty())
		return true;
	const uint64_t hash = std::hash<std::string_view>{}(id);
	if (!ids_.insert(hash).second)
		return false;
	order_.push_back(hash);
	if (order_.size() > capacity_) {
		ids_.erase(order_.front());
		order_.pop_front();
	}
	return true;
}

ChatSession::ChatSession(HttpClient &http, oauth::Provider provider, oauth::Token token, std::string videoId,
			 int minPollMs, TokenChanged onTokenChanged)
	: http_(http),
	  provider_(std::move(provider)),
	  token_(std::move(token)),
	  videoId_(ExtractVideoId(videoId)),
	  minPollMs_(std::max(minPollMs, 1000)),
	  onTokenChanged_(std::move(onTokenChanged))
{
	state_ = token_.IsValid() ? State::WaitingForBroadcast : State::SignedOut;
}

bool ChatSession::Refresh(int64_t now)
{
	if (token_.refreshToken.empty())
		return false;
	auto res = http_.Post(provider_.tokenUrl, {}, oauth::BuildRefreshBody(provider_, token_.refreshToken),
			      "application/x-www-form-urlencoded");
	if (res.status == 0)
		return false;
	auto parsed = oauth::ParseTokenResponse(res.status, res.body, now, token_.refreshToken);
	if (parsed.status != oauth::PollStatus::Granted) {
		token_ = {};
		state_ = State::SignedOut;
		if (onTokenChanged_)
			onTokenChanged_(token_);
		return false;
	}
	token_ = parsed.token;
	if (onTokenChanged_)
		onTokenChanged_(token_);
	return true;
}

HttpResponse ChatSession::Authorized(bool post, const std::string &url, const std::string &body, int64_t now)
{
	if (token_.NeedsRefresh(now))
		Refresh(now);
	if (!token_.IsValid())
		return {401, {}, "signed out"};

	auto send = [&]() {
		std::vector<std::string> headers{"Authorization: Bearer " + token_.accessToken};
		return post ? http_.Post(url, headers, body, "application/json") : http_.Get(url, headers);
	};

	HttpResponse res = send();
	if (res.status == 401 && Refresh(now))
		res = send();
	return res;
}

int ChatSession::HandleError(const HttpResponse &res, StepResult &out)
{
	if (res.status == 0) {
		out.notices.push_back("YouTube: network error (" + res.error + "), retrying");
		return kNetworkRetryMs;
	}
	if (state_ == State::SignedOut) {
		out.notices.push_back("YouTube: sign-in expired, sign in again from the chat settings");
		return kErrorRetryMs;
	}

	std::string reason = ParseErrorReason(res.body);
	if (reason == "quotaExceeded" || reason == "rateLimitExceeded" || res.status == 429) {
		state_ = State::Error;
		out.notices.push_back("YouTube: API quota exceeded, pausing for 15 minutes");
		return kQuotaRetryMs;
	}
	if (reason == "liveChatEnded" || reason == "liveChatNotFound" || reason == "liveChatDisabled" ||
	    res.status == 404) {
		liveChatId_.clear();
		pageToken_.clear();
		state_ = State::WaitingForBroadcast;
		announcedWaiting_ = false;
		out.notices.push_back("YouTube: live chat is no longer available");
		return searchMs_;
	}

	state_ = State::Error;
	out.notices.push_back("YouTube: request failed (HTTP " + std::to_string(res.status) +
			      (reason.empty() ? std::string() : ", " + reason) + ")");
	return kErrorRetryMs;
}

void ChatSession::FindLiveChat(int64_t now, StepResult &out)
{
	std::string url;
	if (!videoId_.empty()) {
		url = std::string(kApiBase) + "/videos?part=liveStreamingDetails&id=" + UrlEncode(videoId_) +
		      kVideoFields;
	} else {
		url = std::string(kApiBase) +
		      "/liveBroadcasts?part=snippet&broadcastStatus=active&broadcastType=all&maxResults=5" +
		      kBroadcastFields;
	}

	HttpResponse res = Authorized(false, url, {}, now);
	if (!res.Ok()) {
		out.nextDelayMs = HandleError(res, out);
		return;
	}

	auto chatId = videoId_.empty() ? ParseBroadcastLiveChatId(res.body) : ParseVideoLiveChatId(res.body);
	if (!chatId) {
		state_ = State::WaitingForBroadcast;
		if (!announcedWaiting_) {
			out.notices.push_back("YouTube: waiting for a live broadcast");
			announcedWaiting_ = true;
		}
		out.nextDelayMs = now < fastSearchUntil_ ? fastSearchMs_ : searchMs_;
		return;
	}

	liveChatId_ = *chatId;
	pageToken_.clear();
	state_ = State::Polling;
	announcedWaiting_ = false;
	streamFailed_ = false; // a new chat gets a fresh try at streaming
	out.notices.push_back(streaming_ ? "YouTube: connected to live chat (streaming)"
					 : "YouTube: connected to live chat");

	if (ownChannelId_.empty()) {
		HttpResponse channel = Authorized(
			false, std::string(kApiBase) + "/channels?part=snippet&mine=true" + kChannelFields, {}, now);
		if (auto own = channel.Ok() ? ParseOwnChannel(channel.body) : std::nullopt) {
			ownChannelId_ = std::move(own->id);
			ownName_ = std::move(own->title);
		}
	}
	out.nextDelayMs = 0;
}

void ChatSession::Poll(int64_t now, StepResult &out)
{
	std::string url = std::string(kApiBase) + "/liveChat/messages?part=snippet,authorDetails&maxResults=200" +
			  "&liveChatId=" + UrlEncode(liveChatId_) + kMessagesFields;
	if (!pageToken_.empty())
		url += "&pageToken=" + UrlEncode(pageToken_);

	HttpResponse res = Authorized(false, url, {}, now);
	if (!res.Ok()) {
		out.nextDelayMs = HandleError(res, out);
		return;
	}

	auto page = ParseMessagesPage(res.body, ownChannelId_);
	if (!page) {
		out.notices.push_back("YouTube: unexpected response from the live chat API");
		out.nextDelayMs = kErrorRetryMs;
		return;
	}

	state_ = State::Polling;
	for (auto &chat : page->messages) {
		if (seen_.Insert(chat.id) && !IsHistory(chat))
			out.messages.push_back(std::move(chat));
	}

	if (page->chatEnded) {
		EndChat(out);
		return;
	}

	if (!page->nextPageToken.empty())
		pageToken_ = page->nextPageToken;
	out.nextDelayMs = std::max(page->pollingIntervalMs, minPollMs_);
}

void ChatSession::EndChat(StepResult &out)
{
	liveChatId_.clear();
	pageToken_.clear();
	state_ = State::WaitingForBroadcast;
	out.notices.push_back("YouTube: live chat ended");
	out.nextDelayMs = searchMs_;
}

void ChatSession::Deliver(std::vector<ChatMessage> &messages, StepResult &out)
{
	std::vector<ChatMessage> fresh;
	for (auto &chat : messages) {
		if (seen_.Insert(chat.id) && !IsHistory(chat))
			fresh.push_back(std::move(chat));
	}
	if (fresh.empty())
		return;
	if (liveSink_)
		liveSink_(std::move(fresh));
	else
		std::move(fresh.begin(), fresh.end(), std::back_inserter(out.messages));
}

void ChatSession::FallBackToPolling(const std::string &reason, StepResult &out)
{
	streamFailed_ = true;
	pageToken_.clear(); // stream and list page tokens aren't documented as interchangeable; dedup covers the replay
	out.notices.push_back("YouTube: chat streaming unavailable (" + reason + "), polling instead");
	out.nextDelayMs = 0;
}

void ChatSession::Stream(int64_t now, StepResult &out)
{
	if (token_.NeedsRefresh(now))
		Refresh(now);
	if (!token_.IsValid()) {
		out.nextDelayMs = HandleError({401, {}, "signed out"}, out);
		return;
	}

	std::string url = std::string(kApiBase) +
			  "/liveChat/messages/stream?part=snippet,authorDetails&maxResults=200&liveChatId=" +
			  UrlEncode(liveChatId_) + kMessagesFields;
	if (!pageToken_.empty())
		url += "&pageToken=" + UrlEncode(pageToken_);

	// The body is a JSON array of list responses, each sent as soon as it's ready; the last one carries the
	// token to resume with.
	JsonArrayReader reader;
	int responses = 0;
	bool ended = false;
	bool malformed = false;
	auto onData = [&](std::string_view chunk) {
		for (const auto &object : reader.Feed(chunk)) {
			auto page = ParseMessagesPage(object, ownChannelId_);
			if (!page) {
				malformed = true;
				return false;
			}
			++responses;
			if (!page->nextPageToken.empty())
				pageToken_ = page->nextPageToken;
			Deliver(page->messages, out);
			if (page->chatEnded) {
				ended = true;
				return false;
			}
		}
		if (reader.Failed()) {
			malformed = true;
			return false;
		}
		return true;
	};

	HttpResponse res = http_.GetStream(url, {"Authorization: Bearer " + token_.accessToken}, onData, interrupt_);
	if (ended) {
		EndChat(out);
		return;
	}
	if (malformed) {
		FallBackToPolling("unexpected data", out);
		return;
	}
	if (res.interrupted) {
		out.nextDelayMs = 0; // resume from pageToken_ after the caller's work
		return;
	}
	if (res.status == 401 && Refresh(now)) {
		out.nextDelayMs = 0;
		return;
	}
	if (!res.Ok()) {
		// Known conditions keep their usual handling; anything else suggests streaming itself doesn't work here.
		std::string reason = ParseErrorReason(res.body);
		bool known = res.status == 0 || res.status == 401 || res.status == 404 || res.status == 429 ||
			     reason == "quotaExceeded" || reason == "rateLimitExceeded" || reason == "liveChatEnded" ||
			     reason == "liveChatNotFound" || reason == "liveChatDisabled";
		if (known)
			out.nextDelayMs = HandleError(res, out);
		else
			FallBackToPolling("HTTP " + std::to_string(res.status) + (reason.empty() ? "" : ", " + reason),
					  out);
		return;
	}
	if (responses == 0 || !reader.Finished()) {
		FallBackToPolling("empty stream", out);
		return;
	}
	state_ = State::Polling;
	out.nextDelayMs = 0; // the server ended this stream normally; open the next one right away
}

StepResult ChatSession::Step(int64_t now)
{
	StepResult out;
	if (!token_.IsValid()) {
		state_ = State::SignedOut;
		out.nextDelayMs = kErrorRetryMs;
		return out;
	}
	if (liveChatId_.empty())
		FindLiveChat(now, out);
	else if (IsStreaming())
		Stream(now, out);
	else
		Poll(now, out);
	return out;
}

SendResult ChatSession::Send(std::string_view text, int64_t now)
{
	SendResult result;
	if (!CanSend()) {
		result.error = "YouTube chat is not connected";
		return result;
	}

	HttpResponse res = Authorized(true, std::string(kApiBase) + "/liveChat/messages?part=snippet",
				      BuildInsertBody(liveChatId_, text), now);
	if (!res.Ok()) {
		std::string reason = ParseErrorReason(res.body);
		result.error = res.status == 0 ? res.error
					       : "HTTP " + std::to_string(res.status) +
							 (reason.empty() ? std::string() : " (" + reason + ")");
		return result;
	}

	result.ok = true;
	if (auto chat = ParseChatMessage(res.body)) {
		seen_.Insert(chat->id);
		if (chat->author.empty())
			chat->author = ownName_.empty() ? std::string("You") : ownName_;
		chat->isSelf = true;
		chat->color = "#ffd600";
		result.echo = std::move(*chat);
	}
	return result;
}

} // namespace unified_chat::youtube
