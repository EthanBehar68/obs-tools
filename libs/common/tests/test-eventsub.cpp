#include <doctest.h>

#include "core/eventsub.hpp"

using namespace unified_chat::eventsub;

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
