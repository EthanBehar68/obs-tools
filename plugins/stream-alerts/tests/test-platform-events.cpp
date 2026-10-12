#include <doctest.h>

#include "core/platform-events.hpp"

#include <json.hpp>

using namespace stream_alerts;

TEST_CASE("A channel.follow event gives who followed")
{
	// payload.event of the example in Twitch's channel.follow v2 reference
	auto follow = FollowFromEvent(nlohmann::json::parse(R"({"user_id": "1234", "user_login": "cool_user",
		"user_name": "Cool_User", "broadcaster_user_id": "1337", "broadcaster_user_login": "cooler_user",
		"broadcaster_user_name": "Cooler_User", "followed_at": "2020-07-15T18:16:11.17106713Z"})"));
	REQUIRE(follow);
	CHECK(follow->kind == AlertKind::Follow);
	CHECK(follow->id == "1234");
	CHECK(follow->name == "Cool_User");
	CHECK(FollowFromEvent(nlohmann::json::parse(R"({"user_id":"5","user_login":"x"})"))->name == "x");
	CHECK_FALSE(FollowFromEvent(nlohmann::json::object()));
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
