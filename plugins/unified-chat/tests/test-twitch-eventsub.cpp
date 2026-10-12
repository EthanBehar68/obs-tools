#include <doctest.h>

#include "core/chat-format.hpp"
#include "core/echo-merger.hpp"
#include "core/twitch-eventsub.hpp"

using namespace unified_chat;
using namespace unified_chat::twitch;
using json = nlohmann::json;

// payload.event of the "Regular Chat Message" example in Twitch's channel.chat.message reference.
static const char *kRegularMessage = R"({
	"broadcaster_user_id": "1971641", "broadcaster_user_login": "streamer", "broadcaster_user_name": "streamer",
	"chatter_user_id": "4145994", "chatter_user_login": "viewer32", "chatter_user_name": "viewer32",
	"message_id": "cc106a89-1814-919d-454c-f4f2f970aae7",
	"message": {"text": "Hi chat", "fragments": [{"type": "text", "text": "Hi chat", "cheermote": null,
		     "emote": null, "mention": null}]},
	"color": "#00FF7F",
	"badges": [{"set_id": "moderator", "id": "1", "info": ""}, {"set_id": "subscriber", "id": "12", "info": "16"},
		   {"set_id": "sub-gifter", "id": "1", "info": ""}],
	"message_type": "text", "cheer": null, "reply": null, "channel_points_custom_reward_id": null,
	"source_broadcaster_user_id": null, "source_message_id": null, "source_badges": null})";

TEST_CASE("A channel.chat.message event becomes a chat line")
{
	auto chat = ChatMessageFromEvent(json::parse(kRegularMessage), "1971641");
	REQUIRE(chat);
	CHECK(chat->platform == Platform::Twitch);
	CHECK(chat->id == "cc106a89-1814-919d-454c-f4f2f970aae7");
	CHECK(chat->author == "viewer32");
	CHECK(chat->mention == "viewer32");
	CHECK(chat->authorId == "4145994");
	CHECK(chat->text == "Hi chat");
	CHECK(chat->color == "#00ff7f");
	CHECK_FALSE(chat->isSelf);
	CHECK(chat->emotes.empty());
	REQUIRE(chat->badges.size() == 2); // role badges only, in display order
	CHECK(chat->badges[0].set == "moderator");
	CHECK(chat->badges[1].set == "subscriber");
	CHECK(chat->badges[1].version == "12");

	CHECK(ChatMessageFromEvent(json::parse(kRegularMessage), "4145994")->isSelf);
	CHECK_FALSE(ChatMessageFromEvent(json::parse(R"({"chatter_user_login":"x"})"), "1"));
}

TEST_CASE("Emote fragments give ranges in characters, like the IRC tag")
{
	auto chat = ChatMessageFromEvent(json::parse(R"({"message_id":"m1","chatter_user_id":"7",
		"chatter_user_login":"fan","chatter_user_name":"Fän",
		"message":{"text":"héllo Kappa and Kappa","fragments":[
			{"type":"text","text":"héllo "},
			{"type":"emote","text":"Kappa","emote":{"id":"25","emote_set_id":"0","format":["static"]}},
			{"type":"text","text":" and "},
			{"type":"emote","text":"Kappa","emote":{"id":"25"}}]}})"),
					 "");
	REQUIRE(chat);
	CHECK(chat->author == "Fän");
	CHECK(chat->text == "héllo Kappa and Kappa");
	REQUIRE(chat->emotes.size() == 2);
	CHECK(chat->emotes[0].id == "25");
	CHECK(chat->emotes[0].start == 6); // "héllo " is 6 characters, 7 bytes
	CHECK(chat->emotes[0].end == 10);
	CHECK(chat->emotes[1].start == 16);
	CHECK(chat->emotes[1].end == 20);

	// No fragments: the plain text.
	auto plain = ChatMessageFromEvent(json::parse(R"({"message_id":"m2","message":{"text":"hi"}})"), "");
	REQUIRE(plain);
	CHECK(plain->text == "hi");
}

TEST_CASE("A reply names its parent and drops the leading @parent, moving emotes with it")
{
	auto chat = ChatMessageFromEvent(json::parse(R"({"message_id":"m3","chatter_user_login":"a",
		"message":{"text":"@Dezad hi Kappa","fragments":[
			{"type":"mention","text":"@Dezad","mention":{"user_login":"dezad"}},
			{"type":"text","text":" hi "},
			{"type":"emote","text":"Kappa","emote":{"id":"25"}}]},
		"reply":{"parent_message_id":"p1","parent_user_login":"dezad","parent_user_name":"Dezad"}})"),
					 "");
	REQUIRE(chat);
	CHECK(chat->replyTo == "Dezad");
	CHECK(chat->text == "hi Kappa");
	REQUIRE(chat->emotes.size() == 1);
	CHECK(chat->emotes[0].start == 3);
	CHECK(chat->emotes[0].end == 7);
}

TEST_CASE("Moderation events become strike-throughs")
{
	auto deleted = ModerationFromEvent("channel.chat.message_delete",
					   json::parse(R"({"broadcaster_user_id":"1","target_user_id":"7",
		"target_user_login":"troll","target_user_name":"Troll","message_id":"ab-12"})"));
	REQUIRE(deleted);
	CHECK(deleted->kind == ModerationEvent::Kind::DeleteMessage);
	CHECK(deleted->messageId == "ab-12");
	CHECK(deleted->userLogin == "troll");

	auto cleared = ModerationFromEvent("channel.chat.clear", json::parse(R"({"broadcaster_user_id":"1"})"));
	REQUIRE(cleared);
	CHECK(cleared->kind == ModerationEvent::Kind::ClearChat);

	// Without channel.ban there's no telling a timeout from a ban.
	auto removed = ModerationFromEvent("channel.chat.clear_user_messages",
					   json::parse(R"({"target_user_id":"7","target_user_login":"troll",
		"target_user_name":"Troll"})"));
	REQUIRE(removed);
	CHECK(removed->kind == ModerationEvent::Kind::RemoveUser);
	CHECK(removed->userId == "7");
	CHECK(removed->durationSeconds == kRemovalUnknown);
	CHECK(TagFor(*removed).label == "(removed)");
	CHECK(ModerationNotice(*removed, "Troll") == "Twitch: Troll was timed out or banned");

	auto timeout = ModerationFromEvent("channel.ban", json::parse(R"({"user_id":"7","user_login":"troll",
		"user_name":"Troll","banned_at":"2020-07-15T18:15:11.17106713Z","ends_at":"2020-07-15T18:16:11.17106713Z",
		"is_permanent":false})"));
	REQUIRE(timeout);
	CHECK(timeout->durationSeconds == 60);
	CHECK(TagFor(*timeout).label == "(timed out 1 minute)");

	auto ban = ModerationFromEvent("channel.ban", json::parse(R"({"user_id":"7","user_login":"troll",
		"banned_at":"2020-07-15T18:15:11Z","ends_at":null,"is_permanent":true})"));
	REQUIRE(ban);
	CHECK(ban->durationSeconds == 0);
	CHECK(ban->userName == "troll");
	CHECK(TagFor(*ban).label == "(banned)");

	CHECK_FALSE(ModerationFromEvent("channel.chat.message_delete", json::object()));
	CHECK_FALSE(ModerationFromEvent("channel.follow", json::object()));
}

TEST_CASE("Chat subscriptions: channel.ban replaces clear_user_messages for the broadcaster")
{
	auto types = [](const std::vector<std::string> &bodies) {
		std::vector<std::string> out;
		for (const auto &body : bodies)
			out.push_back(json::parse(body)["type"].get<std::string>());
		return out;
	};
	auto viewer = BuildChatSubscriptions("s1", "100", "200", false);
	CHECK(types(viewer) == std::vector<std::string>{"channel.chat.message", "channel.chat.message_delete",
							"channel.chat.clear", "channel.chat.clear_user_messages"});
	auto first = json::parse(viewer[0]);
	CHECK(first["condition"]["broadcaster_user_id"] == "100");
	CHECK(first["condition"]["user_id"] == "200");
	CHECK(first["transport"]["session_id"] == "s1");

	auto broadcaster = BuildChatSubscriptions("s1", "100", "100", true);
	CHECK(types(broadcaster).back() == "channel.ban");
	CHECK(json::parse(broadcaster.back())["condition"].size() == 1);
}

TEST_CASE("Sending: the request body and Twitch's answer")
{
	auto body = BuildSendChatBody("100", "200", "  hello\nthere ", "p-1");
	REQUIRE(body);
	auto parsed = json::parse(*body);
	CHECK(parsed["broadcaster_id"] == "100");
	CHECK(parsed["sender_id"] == "200");
	CHECK(parsed["message"] == "hello there");
	CHECK(parsed["reply_parent_message_id"] == "p-1");
	CHECK_FALSE(json::parse(*BuildSendChatBody("100", "200", "hi")).contains("reply_parent_message_id"));
	CHECK_FALSE(BuildSendChatBody("100", "200", "   "));
	CHECK_FALSE(BuildSendChatBody("", "200", "hi"));

	auto sent = ParseSendChatResponse(R"({"data":[{"message_id":"abc-123-def","is_sent":true}]})");
	REQUIRE(sent);
	CHECK(sent->sent);
	CHECK(sent->messageId == "abc-123-def");

	auto dropped = ParseSendChatResponse(R"({"data":[{"message_id":"","is_sent":false,
		"drop_reason":{"code":"msg_duplicate","message":"Your message is identical to the one you sent"}}]})");
	REQUIRE(dropped);
	CHECK_FALSE(dropped->sent);
	CHECK(dropped->dropReason == "Your message is identical to the one you sent");
	CHECK_FALSE(ParseSendChatResponse("{}"));
}

TEST_CASE("Get Users gives ids and display names")
{
	auto users = ParseUsers(R"({"data":[{"id":"141981764","login":"twitchdev","display_name":"TwitchDev"},
		{"login":"noid"}]})");
	REQUIRE(users.size() == 1);
	CHECK(users[0].id == "141981764");
	CHECK(users[0].displayName == "TwitchDev");
	CHECK(ParseUsers("nope").empty());
}

TEST_CASE("Twitch's copy of a sent message updates its echo")
{
	ChatMessage echo;
	echo.platform = Platform::Twitch;
	echo.id = "m1";
	echo.author = "me";
	echo.text = "hi Kappa";
	echo.isSelf = true;
	ChatMessage copy = echo;
	copy.author = "Me";
	copy.emotes = {{"25", 3, 7}};
	copy.badges = {{"broadcaster", "1"}};
	copy.color = "#ff0000";

	// Still waiting for YouTube: the pending echo takes the copy's content.
	EchoMerger merger;
	uint64_t sendId = merger.Begin({Platform::Twitch, Platform::YouTube}, 0);
	ChatMessage twitchEcho = echo;
	twitchEcho.sendId = sendId;
	CHECK(merger.Offer(twitchEcho).empty());
	CHECK(merger.Update(copy));
	auto lines = merger.Fail(sendId, Platform::YouTube);
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].message.emotes.size() == 1);
	CHECK(lines[0].message.author == "Me");
	CHECK_FALSE(merger.Update(copy)); // released: nothing pending any more

	// A merged line keeps its joined names.
	ChatMessage merged = echo;
	merged.author = "me / MeOnYouTube";
	ApplyPlatformCopy(merged, copy, true);
	CHECK(merged.author == "me / MeOnYouTube");
	CHECK(merged.color == "#ff0000");
	CHECK(merged.badges.size() == 1);
}
