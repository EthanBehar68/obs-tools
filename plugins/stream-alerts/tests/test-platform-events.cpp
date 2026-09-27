#include <doctest.h>

#include "core/platform-events.hpp"

#include <json.hpp>

using namespace stream_alerts;

// The examples from Twitch's EventSub WebSocket and channel.follow v2 reference.
TEST_CASE("EventSub welcome, keepalive and reconnect messages are read")
{
	auto welcome = ParseEventSubMessage(R"({
		"metadata": {"message_id": "96a3f3b5", "message_type": "session_welcome",
			     "message_timestamp": "2023-07-19T14:56:51.634234626Z"},
		"payload": {"session": {"id": "AQoQILE98gtqShGmLD7AM6yJThAB", "status": "connected",
			    "connected_at": "2023-07-19T14:56:51.616329898Z", "keepalive_timeout_seconds": 60,
			    "reconnect_url": null}}})");
	REQUIRE(welcome);
	CHECK(welcome->type == EventSubMessage::Type::Welcome);
	CHECK(welcome->sessionId == "AQoQILE98gtqShGmLD7AM6yJThAB");
	CHECK(welcome->keepaliveSeconds == 60);

	auto keepalive = ParseEventSubMessage(
		R"({"metadata":{"message_id":"8","message_type":"session_keepalive"},"payload":{}})");
	REQUIRE(keepalive);
	CHECK(keepalive->type == EventSubMessage::Type::Keepalive);

	auto reconnect = ParseEventSubMessage(R"({
		"metadata": {"message_id": "84c1e79a", "message_type": "session_reconnect"},
		"payload": {"session": {"id": "AQoQexAWVYKSTIu4ec_2VAxyuhAB", "status": "reconnecting",
			    "keepalive_timeout_seconds": null, "reconnect_url": "wss://eventsub.wss.twitch.tv?x=1"}}})");
	REQUIRE(reconnect);
	CHECK(reconnect->type == EventSubMessage::Type::Reconnect);
	CHECK(reconnect->reconnectUrl == "wss://eventsub.wss.twitch.tv?x=1");

	CHECK_FALSE(ParseEventSubMessage("not json"));
	CHECK_FALSE(ParseEventSubMessage(R"({"metadata":{"message_type":"session_welcome"},"payload":{}})"));
}

TEST_CASE("A channel.follow notification gives who followed")
{
	auto follow = ParseEventSubMessage(R"({
		"metadata": {"message_id": "befa7b53", "message_type": "notification",
			     "subscription_type": "channel.follow", "subscription_version": "2"},
		"payload": {"subscription": {"id": "f1c2a387", "status": "enabled", "type": "channel.follow",
			    "version": "2", "cost": 0},
			    "event": {"user_id": "1234", "user_login": "cool_user", "user_name": "Cool_User",
			    "broadcaster_user_id": "1337", "broadcaster_user_login": "cooler_user",
			    "broadcaster_user_name": "Cooler_User", "followed_at": "2020-07-15T18:16:11.17106713Z"}}})");
	REQUIRE(follow);
	CHECK(follow->type == EventSubMessage::Type::Notification);
	CHECK(follow->subscriptionType == "channel.follow");
	CHECK(follow->userId == "1234");
	CHECK(follow->userName == "Cool_User");

	auto revoked = ParseEventSubMessage(R"({"metadata":{"message_type":"revocation",
		"subscription_type":"channel.follow"},"payload":{"subscription":{"status":"authorization_revoked"}}})");
	REQUIRE(revoked);
	CHECK(revoked->type == EventSubMessage::Type::Revocation);
	CHECK(revoked->status == "authorization_revoked");
}

TEST_CASE("The follow subscription targets the signed-in channel over this WebSocket session")
{
	auto body = nlohmann::json::parse(BuildFollowSubscription("session1", "1337"));
	CHECK(body["type"] == "channel.follow");
	CHECK(body["version"] == "2");
	CHECK(body["condition"]["broadcaster_user_id"] == "1337");
	CHECK(body["condition"]["moderator_user_id"] == "1337");
	CHECK(body["transport"]["method"] == "websocket");
	CHECK(body["transport"]["session_id"] == "session1");
}

TEST_CASE("Validate gives the user id and scopes; Helix errors give their message")
{
	auto user = ParseValidate(R"({"client_id":"c","login":"me","scopes":["chat:read","moderator:read:followers"],
		"user_id":"1337","expires_in":5000})");
	REQUIRE(user);
	CHECK(user->id == "1337");
	CHECK(user->HasScope(kFollowScope));
	CHECK_FALSE(ParseValidate(R"({"client_id":"c","scopes":[]})")); // an app token has no user
	CHECK_FALSE(ParseValidate(R"({"user_id":"1","scopes":["chat:read"]})")->HasScope(kFollowScope));
	CHECK(ParseHelixMessage(R"({"error":"Forbidden","status":403,"message":"missing scope"})") == "missing scope");
}

TEST_CASE("Recent subscribers: the first list is the baseline, later ones give who is new, oldest first")
{
	auto first = ParseRecentSubscribers(R"({"items":[
		{"subscriberSnippet":{"channelId":"UC2","title":"Two"}},
		{"subscriberSnippet":{"channelId":"UC1","title":"One"}}]})");
	REQUIRE(first);
	REQUIRE(first->size() == 2);
	CHECK((*first)[0].name == "Two");

	SubscriberWatch watch;
	CHECK(watch.Update(*first).empty());
	auto later = ParseRecentSubscribers(R"({"items":[
		{"subscriberSnippet":{"channelId":"UC4","title":"Four"}},
		{"subscriberSnippet":{"channelId":"UC3","title":"Three"}},
		{"subscriberSnippet":{"channelId":"UC2","title":"Two"}}]})");
	auto fresh = watch.Update(*later);
	REQUIRE(fresh.size() == 2);
	CHECK(fresh[0].name == "Three");
	CHECK(fresh[1].name == "Four");
	CHECK(watch.Update(*later).empty());

	CHECK(ParseRecentSubscribers("{}")->empty()); // no subscribers: fields= leaves out the empty list
	CHECK_FALSE(ParseRecentSubscribers(R"({"items":5})"));
	CHECK_FALSE(ParseRecentSubscribers("<html>"));
	CHECK(RecentSubscribersUrl().find("myRecentSubscribers=true") != std::string::npos);
}
