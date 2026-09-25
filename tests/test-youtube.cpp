#include <doctest.h>

#include "core/youtube-api.hpp"

#include <deque>

using namespace unified_chat;
using namespace unified_chat::youtube;

namespace {

// Serves scripted responses in order and records every request.
class FakeHttp : public HttpClient {
public:
	struct Request {
		bool post;
		std::string url;
		std::vector<std::string> headers;
		std::string body;
	};

	void Queue(long status, std::string body) { responses.push_back({status, std::move(body), {}}); }
	void QueueNetworkError() { responses.push_back({0, {}, "timeout"}); }

	HttpResponse Get(const std::string &url, const std::vector<std::string> &headers) override
	{
		return Next({false, url, headers, {}});
	}

	HttpResponse Post(const std::string &url, const std::vector<std::string> &headers, const std::string &body,
			  const std::string &) override
	{
		return Next({true, url, headers, body});
	}

	std::deque<HttpResponse> responses;
	std::vector<Request> requests;

private:
	HttpResponse Next(Request request)
	{
		requests.push_back(std::move(request));
		REQUIRE_MESSAGE(!responses.empty(), "unexpected request: " << requests.back().url);
		auto res = responses.front();
		responses.pop_front();
		return res;
	}
};

const char *kBroadcastLive = R"({"items":[{"id":"vid","snippet":{"liveChatId":"CHAT1"}}]})";
const char *kChannel = R"({"items":[{"id":"UCme","snippet":{"title":"My Channel"}}]})";

std::string Page(const std::string &items, const std::string &token = "p2", int interval = 2000)
{
	return R"({"nextPageToken":")" + token + R"(","pollingIntervalMillis":)" + std::to_string(interval) +
	       R"(,"items":[)" + items + "]}";
}

std::string Item(const std::string &id, const std::string &name, const std::string &text,
		 const std::string &channelId = "UCviewer")
{
	return R"({"id":")" + id + R"(","snippet":{"type":"textMessageEvent","displayMessage":")" + text +
	       R"("},"authorDetails":{"displayName":")" + name + R"(","channelId":")" + channelId + R"("}})";
}

oauth::Token FreshToken()
{
	return {"access", "refresh", 100000};
}

} // namespace

TEST_CASE("ParseMessagesPage extracts messages, token and interval")
{
	auto page = ParseMessagesPage(
		R"({"nextPageToken":"n","pollingIntervalMillis":3000,"items":[
		{"id":"1","snippet":{"type":"textMessageEvent","displayMessage":"hello"},
		 "authorDetails":{"displayName":"Ann","isChatModerator":true}},
		{"id":"2","snippet":{"type":"superChatEvent","displayMessage":"$5.00 thanks!"},
		 "authorDetails":{"displayName":"Bob","isChatOwner":true}},
		{"id":"3","snippet":{"type":"messageDeletedEvent"},"authorDetails":{"displayName":"X"}}]})");
	REQUIRE(page);
	CHECK(page->nextPageToken == "n");
	CHECK(page->pollingIntervalMs == 3000);
	CHECK_FALSE(page->chatEnded);
	REQUIRE(page->messages.size() == 2);
	CHECK(page->messages[0].platform == Platform::YouTube);
	CHECK(page->messages[0].author == "Ann");
	CHECK(page->messages[0].text == "hello");
	CHECK(page->messages[0].color == "#5e84f1");
	CHECK(page->messages[1].color == "#ffd600");
}

TEST_CASE("ParseMessagesPage detects the end of chat")
{
	auto offline = ParseMessagesPage(R"({"offlineAt":"2026-09-23T00:00:00Z","items":[]})");
	REQUIRE(offline);
	CHECK(offline->chatEnded);
	auto event = ParseMessagesPage(R"({"items":[{"id":"e","snippet":{"type":"chatEndedEvent"}}]})");
	REQUIRE(event);
	CHECK(event->chatEnded);
	CHECK(event->messages.empty());
	CHECK_FALSE(ParseMessagesPage("[]"));
	CHECK_FALSE(ParseMessagesPage("garbage"));
}

TEST_CASE("ParseMessagesPage marks messages from your own channel")
{
	auto page = ParseMessagesPage(Page(Item("1", "Me", "from my phone", "UCme") + "," + Item("2", "Ann", "hi") +
					   "," + Item("3", "NoId", "x", "")),
				      "UCme");
	REQUIRE(page);
	REQUIRE(page->messages.size() == 3);
	CHECK(page->messages[0].isSelf);
	CHECK_FALSE(page->messages[1].isSelf);
	CHECK_FALSE(page->messages[2].isSelf);

	auto unknown = ParseMessagesPage(Page(Item("4", "NoId", "x", "")));
	REQUIRE(unknown);
	CHECK_FALSE(unknown->messages[0].isSelf); // an unknown own ID never matches a missing author ID
}

TEST_CASE("ParseOwnChannel reads the signed-in channel")
{
	auto own = ParseOwnChannel(kChannel);
	REQUIRE(own);
	CHECK(own->id == "UCme");
	CHECK(own->title == "My Channel");
	CHECK_FALSE(ParseOwnChannel(R"({"items":[]})"));
	CHECK_FALSE(ParseOwnChannel("garbage"));
}

TEST_CASE("Live chat ID parsing")
{
	CHECK(ParseBroadcastLiveChatId(kBroadcastLive) == "CHAT1");
	CHECK_FALSE(ParseBroadcastLiveChatId(R"({"items":[]})"));
	CHECK(ParseVideoLiveChatId(R"({"items":[{"liveStreamingDetails":{"activeLiveChatId":"C2"}}]})") == "C2");
	CHECK_FALSE(ParseVideoLiveChatId(R"({"items":[{"liveStreamingDetails":{}}]})"));
}

TEST_CASE("ParseErrorReason reads Google API errors")
{
	CHECK(ParseErrorReason(R"({"error":{"code":403,"message":"m","errors":[{"reason":"quotaExceeded"}]}})") ==
	      "quotaExceeded");
	CHECK(ParseErrorReason(R"({"error":{"code":401,"message":"Invalid Credentials"}})") == "Invalid Credentials");
	CHECK(ParseErrorReason(R"({"error":"invalid_grant"})") == "invalid_grant");
	CHECK(ParseErrorReason("") == "");
}

TEST_CASE("BuildInsertBody escapes JSON and strips line breaks")
{
	auto body = BuildInsertBody("CHAT", "say \"hi\"\nnow");
	CHECK(body ==
	      R"({"snippet":{"liveChatId":"CHAT","textMessageDetails":{"messageText":"say \"hi\" now"},"type":"textMessageEvent"}})");
}

TEST_CASE("ExtractVideoId accepts IDs and common URL shapes")
{
	CHECK(ExtractVideoId("dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(ExtractVideoId("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=10") == "dQw4w9WgXcQ");
	CHECK(ExtractVideoId("https://youtu.be/dQw4w9WgXcQ?si=abc") == "dQw4w9WgXcQ");
	CHECK(ExtractVideoId("https://www.youtube.com/live/dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(ExtractVideoId("https://studio.youtube.com/video/dQw4w9WgXcQ/livestreaming") == "dQw4w9WgXcQ");
	CHECK(ExtractVideoId("") == "");
	CHECK(ExtractVideoId("too-short") == "");
}

TEST_CASE("RecentIds deduplicates within capacity")
{
	RecentIds ids(2);
	CHECK(ids.Insert("a"));
	CHECK_FALSE(ids.Insert("a"));
	CHECK(ids.Insert("b"));
	CHECK(ids.Insert("c")); // evicts "a"
	CHECK(ids.Size() == 2);
	CHECK(ids.Insert("a"));
	CHECK(ids.Insert("")); // messages without IDs are never dropped
}

TEST_CASE("ChatSession finds the active broadcast then polls with page tokens")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), FreshToken(), "", 5000, nullptr);
	CHECK(session.GetState() == State::WaitingForBroadcast);

	http.Queue(200, kBroadcastLive);
	http.Queue(200, kChannel);
	auto first = session.Step(0);
	CHECK(session.GetState() == State::Polling);
	CHECK(session.LiveChatId() == "CHAT1");
	CHECK(first.nextDelayMs == 0);
	CHECK(http.requests[0].url.find("broadcastStatus=active") != std::string::npos);
	CHECK(http.requests[0].headers.at(0) == "Authorization: Bearer access");

	http.Queue(200, Page(Item("m1", "Ann", "hi") + "," + Item("m2", "Bob", "yo") + "," +
				     Item("m0", "My Channel", "typed in Studio", "UCme"),
			     "p2", 2000));
	auto poll = session.Step(1);
	REQUIRE(poll.messages.size() == 3);
	CHECK(poll.messages[0].author == "Ann");
	CHECK_FALSE(poll.messages[0].isSelf);
	CHECK(poll.messages[2].isSelf);
	CHECK(poll.nextDelayMs == 5000); // minimum poll interval beats the server's 2000ms

	// Next page repeats m2; only m3 is new.
	http.Queue(200, Page(Item("m2", "Bob", "yo") + "," + Item("m3", "Cid", "new"), "p3", 9000));
	auto again = session.Step(2);
	CHECK(http.requests.back().url.find("pageToken=p2") != std::string::npos);
	// Partial responses: every call asks only for what the parsers read.
	CHECK(http.requests[0].url.find("&fields=items/snippet/liveChatId") != std::string::npos);
	CHECK(http.requests[1].url.find("&fields=items(id,snippet/title)") != std::string::npos);
	const std::string &pollUrl = http.requests.back().url;
	for (const char *field :
	     {"nextPageToken", "pollingIntervalMillis", "offlineAt", "displayMessage", "messageText", "displayName",
	      "channelId", "isChatOwner", "isChatModerator", "isChatSponsor"}) {
		CAPTURE(field);
		CHECK(pollUrl.find(field) != std::string::npos);
	}
	CHECK(http.requests.back().url.find("liveChatId=CHAT1") != std::string::npos);
	REQUIRE(again.messages.size() == 1);
	CHECK(again.messages[0].id == "m3");
	CHECK(again.nextDelayMs == 9000);
}

TEST_CASE("ChatSession waits quietly while no broadcast is live")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), FreshToken(), "", 5000, nullptr);
	http.Queue(200, R"({"items":[]})");
	http.Queue(200, R"({"items":[]})");
	auto first = session.Step(0);
	auto second = session.Step(1);
	CHECK(first.notices.size() == 1);
	CHECK(second.notices.empty());
	CHECK(second.nextDelayMs == 30000);
	CHECK_FALSE(session.CanSend());
}

TEST_CASE("ChatSession uses an explicit video when configured")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), FreshToken(), "https://youtu.be/dQw4w9WgXcQ",
			    5000, nullptr);
	http.Queue(200, R"({"items":[{"liveStreamingDetails":{"activeLiveChatId":"VCHAT"}}]})");
	http.Queue(200, kChannel);
	session.Step(0);
	CHECK(http.requests[0].url.find("/videos?part=liveStreamingDetails&id=dQw4w9WgXcQ") != std::string::npos);
	CHECK(session.LiveChatId() == "VCHAT");
}

TEST_CASE("ChatSession refreshes an expired token before calling the API")
{
	FakeHttp http;
	oauth::Token saved;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), {"old", "refresh", 100}, "", 5000,
			    [&](const oauth::Token &token) { saved = token; });

	http.Queue(200, R"({"access_token":"new","expires_in":3600})");
	http.Queue(200, kBroadcastLive);
	http.Queue(200, kChannel);
	session.Step(90);

	CHECK(http.requests[0].post);
	CHECK(http.requests[0].url == "https://oauth2.googleapis.com/token");
	CHECK(http.requests[1].headers.at(0) == "Authorization: Bearer new");
	CHECK(saved.accessToken == "new");
	CHECK(saved.refreshToken == "refresh");
	CHECK(saved.expiresAt == 3690);
}

TEST_CASE("ChatSession retries once after a 401")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), FreshToken(), "", 5000, nullptr);
	http.Queue(401, R"({"error":{"code":401,"message":"Invalid Credentials"}})");
	http.Queue(200, R"({"access_token":"new","expires_in":3600})");
	http.Queue(200, kBroadcastLive);
	http.Queue(200, kChannel);
	session.Step(0);
	CHECK(session.GetState() == State::Polling);
	CHECK(http.requests[2].headers.at(0) == "Authorization: Bearer new");
}

TEST_CASE("ChatSession signs out when the refresh token is revoked")
{
	FakeHttp http;
	bool cleared = false;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), {"old", "refresh", 100}, "", 5000,
			    [&](const oauth::Token &token) { cleared = !token.IsValid(); });
	http.Queue(400, R"({"error":"invalid_grant"})");
	auto out = session.Step(90);
	CHECK(cleared);
	CHECK(session.GetState() == State::SignedOut);
	CHECK_FALSE(out.notices.empty());
}

TEST_CASE("ChatSession backs off on quota errors and network failures")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), FreshToken(), "", 5000, nullptr);
	http.Queue(403, R"({"error":{"code":403,"errors":[{"reason":"quotaExceeded"}]}})");
	auto quota = session.Step(0);
	CHECK(quota.nextDelayMs == 15 * 60 * 1000);
	CHECK(session.GetState() == State::Error);

	http.QueueNetworkError();
	auto network = session.Step(1);
	CHECK(network.nextDelayMs == 10000);
}

TEST_CASE("ChatSession returns to broadcast search when chat ends")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), FreshToken(), "", 5000, nullptr);
	http.Queue(200, kBroadcastLive);
	http.Queue(200, kChannel);
	session.Step(0);

	http.Queue(200, R"({"offlineAt":"2026-09-23T00:00:00Z","items":[]})");
	auto ended = session.Step(1);
	CHECK(session.GetState() == State::WaitingForBroadcast);
	CHECK(session.LiveChatId().empty());
	CHECK(ended.nextDelayMs == 30000);

	http.Queue(403, R"({"error":{"code":403,"errors":[{"reason":"liveChatEnded"}]}})");
	session.Step(2);
	CHECK(session.GetState() == State::WaitingForBroadcast);
}

TEST_CASE("ChatSession Send posts the message and suppresses the polled duplicate")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), FreshToken(), "", 5000, nullptr);

	auto notConnected = session.Send("hi", 0);
	CHECK_FALSE(notConnected.ok);
	CHECK(http.requests.empty());

	http.Queue(200, kBroadcastLive);
	http.Queue(200, kChannel);
	session.Step(0);

	http.Queue(200, R"({"id":"mine","snippet":{"type":"textMessageEvent",
		"textMessageDetails":{"messageText":"hello chat"}}})");
	auto sent = session.Send("hello chat", 1);
	REQUIRE(sent.ok);
	REQUIRE(sent.echo);
	CHECK(sent.echo->author == "My Channel");
	CHECK(sent.echo->isSelf);
	CHECK(http.requests.back().post);
	CHECK(http.requests.back().body.find("\"liveChatId\":\"CHAT1\"") != std::string::npos);

	http.Queue(200, Page(Item("mine", "My Channel", "hello chat")));
	auto poll = session.Step(2);
	CHECK(poll.messages.empty());
}

TEST_CASE("ChatSession Send reports API errors")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), FreshToken(), "", 5000, nullptr);
	http.Queue(200, kBroadcastLive);
	http.Queue(200, kChannel);
	session.Step(0);

	http.Queue(403, R"({"error":{"code":403,"errors":[{"reason":"forbidden"}]}})");
	auto sent = session.Send("hi", 1);
	CHECK_FALSE(sent.ok);
	CHECK(sent.error == "HTTP 403 (forbidden)");
}

TEST_CASE("ChatSession without a token stays signed out and makes no requests")
{
	FakeHttp http;
	ChatSession session(http, oauth::GoogleProvider("id", "sec"), {}, "", 5000, nullptr);
	CHECK(session.GetState() == State::SignedOut);
	session.Step(0);
	CHECK(http.requests.empty());
}
