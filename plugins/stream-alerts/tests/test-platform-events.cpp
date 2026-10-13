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

using json = nlohmann::json;

TEST_CASE("Twitch subscriptions follow what's wanted")
{
	auto types = [](const std::vector<std::string> &bodies) {
		std::vector<std::string> out;
		for (const auto &body : bodies)
			out.push_back(json::parse(body)["type"].get<std::string>());
		return out;
	};
	CHECK(types(BuildTwitchSubscriptions("s", "1", {true, true, true})) ==
	      std::vector<std::string>{"channel.follow", "channel.chat.notification", "channel.cheer"});
	CHECK(types(BuildTwitchSubscriptions("s", "1", {false, false, true})) ==
	      std::vector<std::string>{"channel.cheer"});
	auto notification = json::parse(BuildTwitchSubscriptions("s", "1", {false, true, false})[0]);
	CHECK(notification["condition"]["broadcaster_user_id"] == "1");
	CHECK(notification["condition"]["user_id"] == "1");
}

TEST_CASE("Chat notifications become sub, gift and raid alerts")
{
	auto sub = NotificationFromEvent(json::parse(R"({"notice_type":"sub","chatter_user_name":"Fan",
		"chatter_is_anonymous":false,"message_id":"m1","message":{"text":""},"sub":{"sub_tier":"1000"}})"));
	REQUIRE(sub);
	CHECK(sub->kind == AlertKind::TwitchSub);
	CHECK_FALSE(sub->variant);
	CHECK(sub->name == "Fan");

	auto resub = NotificationFromEvent(json::parse(R"({"notice_type":"resub","chatter_user_name":"Fan",
		"message":{"text":"hi"},"resub":{"cumulative_months":14,"duration_months":1}})"));
	REQUIRE(resub);
	CHECK(resub->variant);
	CHECK(resub->months == 14);
	CHECK(resub->message == "hi");

	auto bomb = NotificationFromEvent(json::parse(R"({"notice_type":"community_sub_gift",
		"chatter_user_name":"Giver","community_sub_gift":{"id":"g1","total":5,"sub_tier":"1000"}})"));
	REQUIRE(bomb);
	CHECK(bomb->kind == AlertKind::GiftSubs);
	CHECK(bomb->count == 5);
	// Each recipient of that gift bomb: already covered.
	CHECK_FALSE(NotificationFromEvent(json::parse(R"({"notice_type":"sub_gift","chatter_user_name":"Giver",
		"sub_gift":{"recipient_user_name":"Lucky","community_gift_id":"g1"}})")));
	auto single = NotificationFromEvent(json::parse(R"({"notice_type":"sub_gift","chatter_is_anonymous":true,
		"sub_gift":{"recipient_user_name":"Lucky","community_gift_id":null}})"));
	REQUIRE(single);
	CHECK(single->count == 1);
	CHECK(single->name == "Anonymous");

	auto raid = NotificationFromEvent(json::parse(R"({"notice_type":"raid","chatter_user_name":"Pal",
		"raid":{"user_name":"Pal","user_login":"pal","viewer_count":25}})"));
	REQUIRE(raid);
	CHECK(raid->kind == AlertKind::Raid);
	CHECK(raid->viewers == 25);

	CHECK_FALSE(NotificationFromEvent(json::parse(R"({"notice_type":"announcement"})")));
}

TEST_CASE("Cheers give bits and the message; anonymous cheers have no name")
{
	auto cheer = CheerFromEvent(json::parse(R"({"is_anonymous":false,"user_id":"7","user_login":"fan",
		"user_name":"Fan","broadcaster_user_id":"1","message":"cheer100 nice","bits":100})"));
	REQUIRE(cheer);
	CHECK(cheer->kind == AlertKind::Cheer);
	CHECK(cheer->count == 100);
	CHECK(cheer->amount == "100");
	CHECK(cheer->message == "cheer100 nice");
	CHECK(CheerFromEvent(json::parse(R"({"is_anonymous":true,"user_name":null,"bits":5})"))->name == "Anonymous");
	CHECK_FALSE(CheerFromEvent(json::parse(R"({"user_name":"x"})")));
}

TEST_CASE("YouTube events from the chat become alerts")
{
	obs_tools::YouTubeEvent milestone;
	milestone.kind = obs_tools::YouTubeEvent::Kind::MemberMilestone;
	milestone.name = "@old";
	milestone.months = 12;
	auto alert = FromYouTubeEvent(milestone);
	CHECK(alert.kind == AlertKind::Member);
	CHECK(alert.variant);
	CHECK(alert.months == 12);

	obs_tools::YouTubeEvent sticker;
	sticker.kind = obs_tools::YouTubeEvent::Kind::SuperSticker;
	sticker.amount = "$2.00";
	CHECK(FromYouTubeEvent(sticker).kind == AlertKind::SuperChat);

	obs_tools::YouTubeEvent gift;
	gift.kind = obs_tools::YouTubeEvent::Kind::Jewels;
	gift.id = "LCC.g#5";
	gift.gift = "Rose";
	gift.combo = 5;
	gift.count = 3; // gifts 3 to 5 of a combo
	gift.jewels = 10;
	alert = FromYouTubeEvent(gift);
	CHECK(alert.kind == AlertKind::Jewels);
	CHECK(alert.id == "LCC.g#5");
	CHECK(alert.variant); // several gifts
	CHECK(alert.count == 3);
	CHECK(alert.jewels == 30);
	CHECK(alert.amount == "30");
	CHECK(alert.gift == "Rose");
	CHECK(IsPriority(alert.kind));
	CHECK_FALSE(IsTwitch(alert.kind));
	gift.count = 1;
	CHECK_FALSE(FromYouTubeEvent(gift).variant);
}
