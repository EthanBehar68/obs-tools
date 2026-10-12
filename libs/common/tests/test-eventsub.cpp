#include <doctest.h>

#include "core/eventsub.hpp"

using namespace obs_tools::eventsub;

// The examples from Twitch's EventSub WebSocket reference.
TEST_CASE("EventSub welcome, keepalive and reconnect messages are read")
{
	auto welcome = ParseMessage(R"({
		"metadata": {"message_id": "96a3f3b5", "message_type": "session_welcome",
			     "message_timestamp": "2023-07-19T14:56:51.634234626Z"},
		"payload": {"session": {"id": "AQoQILE98gtqShGmLD7AM6yJThAB", "status": "connected",
			    "connected_at": "2023-07-19T14:56:51.616329898Z", "keepalive_timeout_seconds": 60,
			    "reconnect_url": null}}})");
	REQUIRE(welcome);
	CHECK(welcome->type == Message::Type::Welcome);
	CHECK(welcome->sessionId == "AQoQILE98gtqShGmLD7AM6yJThAB");
	CHECK(welcome->keepaliveSeconds == 60);

	auto keepalive =
		ParseMessage(R"({"metadata":{"message_id":"8","message_type":"session_keepalive"},"payload":{}})");
	REQUIRE(keepalive);
	CHECK(keepalive->type == Message::Type::Keepalive);

	auto reconnect = ParseMessage(R"({
		"metadata": {"message_id": "84c1e79a", "message_type": "session_reconnect"},
		"payload": {"session": {"id": "AQoQexAWVYKSTIu4ec_2VAxyuhAB", "status": "reconnecting",
			    "keepalive_timeout_seconds": null, "reconnect_url": "wss://eventsub.wss.twitch.tv?x=1"}}})");
	REQUIRE(reconnect);
	CHECK(reconnect->type == Message::Type::Reconnect);
	CHECK(reconnect->reconnectUrl == "wss://eventsub.wss.twitch.tv?x=1");

	CHECK_FALSE(ParseMessage("not json"));
	CHECK_FALSE(ParseMessage(R"({"metadata":{"message_type":"session_welcome"},"payload":{}})"));
}

TEST_CASE("Notifications carry their type and event; revocations their status")
{
	auto follow = ParseMessage(R"({
		"metadata": {"message_id": "befa7b53", "message_type": "notification",
			     "subscription_type": "channel.follow", "subscription_version": "2"},
		"payload": {"subscription": {"id": "f1c2a387", "status": "enabled", "type": "channel.follow"},
			    "event": {"user_id": "1234", "user_name": "Cool_User"}}})");
	REQUIRE(follow);
	CHECK(follow->type == Message::Type::Notification);
	CHECK(follow->subscriptionType == "channel.follow");
	CHECK(StringAt(follow->event, {"user_id"}) == "1234");
	CHECK(StringAt(follow->event, {"missing", "path"}) == "");

	auto revoked = ParseMessage(R"({"metadata":{"message_type":"revocation",
		"subscription_type":"channel.chat.message"},"payload":{"subscription":{"status":"user_removed"}}})");
	REQUIRE(revoked);
	CHECK(revoked->type == Message::Type::Revocation);
	CHECK(revoked->subscriptionType == "channel.chat.message");
	CHECK(revoked->status == "user_removed");
}

TEST_CASE("A subscription request names the type, condition and WebSocket session")
{
	auto body = nlohmann::json::parse(BuildSubscription(
		"channel.chat.message", "1", {{"broadcaster_user_id", "1971641"}, {"user_id", "2914196"}}, "s1"));
	CHECK(body["type"] == "channel.chat.message");
	CHECK(body["version"] == "1");
	CHECK(body["condition"]["broadcaster_user_id"] == "1971641");
	CHECK(body["condition"]["user_id"] == "2914196");
	CHECK(body["transport"]["method"] == "websocket");
	CHECK(body["transport"]["session_id"] == "s1");
}

namespace {

using Clock = Session::Clock;

std::string Welcome(const char *id, const char *sessionId, int keepalive = 60)
{
	return std::string(R"({"metadata":{"message_id":")") + id +
	       R"(","message_type":"session_welcome"},"payload":{"session":{"id":")" + sessionId +
	       R"(","keepalive_timeout_seconds":)" + std::to_string(keepalive) + "}}}";
}

std::string Notification(const char *id, const char *type = "channel.follow")
{
	return std::string(R"({"metadata":{"message_id":")") + id +
	       R"(","message_type":"notification","subscription_type":")" + type +
	       R"("},"payload":{"event":{"user_id":"1"}}})";
}

std::string Reconnect(const char *id, const char *url)
{
	return std::string(R"({"metadata":{"message_id":")") + id +
	       R"(","message_type":"session_reconnect"},"payload":{"session":{"reconnect_url":")" + url + R"("}}})";
}

} // namespace

TEST_CASE("A session subscribes once, on the first welcome")
{
	const auto t0 = Clock::now();
	Session session(t0);
	CHECK_FALSE(session.Welcomed());
	auto step = session.Handle(Welcome("w1", "s1", 30), false, t0);
	CHECK(step.subscribe == "s1");
	CHECK(session.Welcomed());
	CHECK(session.Deadline() == t0 + std::chrono::seconds(40)); // keepalive 30 s, plus 10 s of slack
	CHECK(session.Handle(Welcome("w2", "s1"), false, t0).subscribe.empty());
}

TEST_CASE("Every message moves the deadline; a resent message is dropped")
{
	const auto t0 = Clock::now();
	Session session(t0);
	session.Handle(Welcome("w1", "s1"), false, t0);
	auto first = session.Handle(Notification("n1"), false, t0 + std::chrono::seconds(5));
	REQUIRE(first.notification);
	CHECK(first.notification->subscriptionType == "channel.follow");
	CHECK(first.notification->messageId == "n1");
	auto again = session.Handle(Notification("n1"), false, t0 + std::chrono::seconds(20));
	CHECK_FALSE(again.notification);                            // at-least-once delivery: the same message_id twice
	CHECK(session.Deadline() == t0 + std::chrono::seconds(90)); // a resend still shows the socket is alive
	CHECK(session.Handle(Notification("n2"), false, t0).notification);
	CHECK_FALSE(session.Handle("not json", false, t0).notification);
}

TEST_CASE("A reconnect opens a second socket, whose welcome takes over without resubscribing")
{
	const auto t0 = Clock::now();
	Session session(t0);
	session.Handle(Welcome("w1", "s1"), false, t0);
	auto reconnect = session.Handle(Reconnect("r1", "wss://eventsub.wss.twitch.tv?id=2"), false, t0);
	CHECK(reconnect.reconnectTo == "wss://eventsub.wss.twitch.tv?id=2");
	CHECK(session.Reconnecting());
	CHECK(session.ReconnectDeadline() == t0 + Session::kReconnectWindow);
	// Still on the old socket meanwhile: its notifications count.
	CHECK(session.Handle(Notification("n1"), false, t0).notification);
	// The same notification on the new socket isn't delivered twice.
	CHECK_FALSE(session.Handle(Notification("n1"), true, t0).notification);
	auto welcome = session.Handle(Welcome("w2", "s2"), true, t0);
	CHECK(welcome.promote);
	CHECK(welcome.subscribe.empty()); // the subscriptions moved with the session
	CHECK_FALSE(session.Reconnecting());
	// A second reconnect request while one is under way is ignored; after giving up, a new one starts.
	session.Handle(Reconnect("r2", "wss://a"), false, t0);
	CHECK(session.Handle(Reconnect("r3", "wss://b"), false, t0).reconnectTo.empty());
	session.AbandonReconnect();
	CHECK(session.Handle(Reconnect("r4", "wss://c"), false, t0).reconnectTo == "wss://c");
}

TEST_CASE("A revocation is reported")
{
	const auto t0 = Clock::now();
	Session session(t0);
	auto step = session.Handle(R"({"metadata":{"message_id":"x","message_type":"revocation",
		"subscription_type":"channel.cheer"},"payload":{"subscription":{"status":"authorization_revoked"}}})",
				   false, t0);
	REQUIRE(step.revocation);
	CHECK(step.revocation->status == "authorization_revoked");
}
