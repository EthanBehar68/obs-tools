#include <doctest.h>

#include "core/chat-config.hpp"
#include "core/chatter-log.hpp"
#include "core/highlights.hpp"
#include "core/name-color.hpp"
#include "core/text-util.hpp"
#include "core/twitch-eventsub.hpp"

#include <json.hpp>

using namespace unified_chat;
using json = nlohmann::json;

TEST_CASE("Every highlight has a default; alerts amber, first-time chatters green, mentions the theme's tint")
{
	Highlights highlights;
	CHECK(highlights[HighlightKind::Follow].color == "#e8a317");
	CHECK(highlights[HighlightKind::Cheer].icon == "\U0001F48E");
	CHECK(highlights[HighlightKind::FirstChatter].color == "#3fb950");
	CHECK(highlights[HighlightKind::Mention].color.empty());
	CHECK(highlights[HighlightKind::Mention].icon.empty());
	for (const auto &h : highlights.kinds)
		CHECK(h.show);
	CHECK(std::string(HighlightKey(HighlightKind::GiftMemberships)) == "gift_memberships");
}

TEST_CASE("Any colour stays readable: the text is adjusted against the tint")
{
	for (const char *background : {"#3c404d", "#ffffff", "#000000"}) {
		for (const char *accent : {"#e8a317", "#3fb950", "#000000", "#ffffff", "#ff00ff"}) {
			const HighlightStyle style = ComputeHighlightStyle(accent, background);
			REQUIRE_FALSE(style.background.empty());
			if (std::string(accent) != background)
				CHECK(style.background != background); // tinted
			CHECK(ContrastRatio(style.text, style.background) >= kMinNameContrast - 0.01);
		}
	}
	CHECK(ComputeHighlightStyle("", "#3c404d").background.empty()); // the mention default: the theme's tint
	CHECK(ComputeHighlightStyle("pink", "#3c404d").text.empty());
}

TEST_CASE("An icon is one emoji at most, cut at a character boundary")
{
	CHECK(SanitizeIcon("  ✨ ") == "✨");
	CHECK(SanitizeIcon("\U0001F468‍\U0001F469‍\U0001F467") ==
	      "\U0001F468‍\U0001F469‍\U0001F467"); // a family: joined emoji fit
	const std::string longer = SanitizeIcon(std::string(20, 'x') + "\U0001F48E\U0001F48E\U0001F48E");
	CHECK(longer.size() <= kMaxIconBytes);
	CHECK(Utf8Length(longer) == 23);      // no half character left at the end
	CHECK(SanitizeIcon("a\nb") == "a b"); // no line breaks
}

TEST_CASE("Highlights round trip through the config; bad values fall back")
{
	ChatConfig config;
	config.highlights[HighlightKind::Raid] = {false, "#ff0000", "\U0001F680"};
	config.highlights[HighlightKind::Mention].color = "#00ffff";
	ChatConfig loaded = ParseConfig(SerializeConfig(config));
	CHECK_FALSE(loaded.highlights[HighlightKind::Raid].show);
	CHECK(loaded.highlights[HighlightKind::Raid].color == "#ff0000");
	CHECK(loaded.highlights[HighlightKind::Raid].icon == "\U0001F680");
	CHECK(loaded.highlights[HighlightKind::Mention].color == "#00ffff");
	CHECK(loaded.highlights[HighlightKind::Cheer].icon == "\U0001F48E"); // untouched kinds keep defaults

	ChatConfig damaged = ParseConfig(R"({"highlights":{"cheer":{"color":"orange","icon":5},"follow":"x"}})");
	CHECK(damaged.highlights[HighlightKind::Cheer].color == "#e8a317");
	CHECK(damaged.highlights[HighlightKind::Cheer].icon == "\U0001F48E");
	CHECK(ParseConfig("{}").highlights[HighlightKind::Follow].show);
}

TEST_CASE("Cheers, Super Chats, memberships and Jewels become event lines; other messages don't")
{
	ChatMessage cheer;
	cheer.author = "Fan";
	cheer.text = "Cheer500 love it";
	cheer.bits = 500;
	auto event = EventFromMessage(cheer);
	REQUIRE(event);
	CHECK(event->kind == HighlightKind::Cheer);
	CHECK(event->text == "Fan cheered 500 bits");
	CHECK(event->comment == "Cheer500 love it");

	ChatMessage superChat;
	superChat.platform = Platform::YouTube;
	superChat.author = "@viewer";
	superChat.youtubeEvent = YouTubeEvent{YouTubeEvent::Kind::SuperChat, "m", "UC1", "@viewer", "$5.00", "hi"};
	event = EventFromMessage(superChat);
	REQUIRE(event);
	CHECK(event->platform == Platform::YouTube);
	CHECK(event->text == "@viewer sent $5.00");
	CHECK(event->comment == "hi");

	superChat.youtubeEvent->kind = YouTubeEvent::Kind::MemberMilestone;
	superChat.youtubeEvent->months = 12;
	superChat.youtubeEvent->level = "Gold";
	CHECK(EventFromMessage(superChat)->text == "@viewer has been a member for 12 months (Gold)");
	CHECK(EventFromMessage(superChat)->kind == HighlightKind::Member);

	YouTubeEvent gift;
	gift.kind = YouTubeEvent::Kind::Jewels;
	gift.gift = "Rose";
	gift.jewels = 10;
	gift.combo = gift.count = 1;
	superChat.youtubeEvent = gift;
	CHECK(EventFromMessage(superChat)->kind == HighlightKind::Jewels);
	CHECK(EventFromMessage(superChat)->text == "@viewer sent Rose (10 Jewels)");
	superChat.youtubeEvent->combo = 5;
	superChat.youtubeEvent->count = 3; // a combo update: gifts 3 to 5
	CHECK(EventFromMessage(superChat)->text == "@viewer sent 3 × Rose (30 Jewels), 5 in a row");

	CHECK_FALSE(EventFromMessage(ChatMessage{}));
}

TEST_CASE("Twitch chat notifications become sub, gift and raid event lines in Twitch's words")
{
	auto resub = twitch::NotificationEvent(json::parse(R"({"notice_type":"resub",
		"system_message":"Fan subscribed at Tier 1. They've subscribed for 10 months!","message":{"text":"hi"}})"));
	REQUIRE(resub);
	CHECK(resub->kind == HighlightKind::TwitchSub);
	CHECK(resub->text == "Fan subscribed at Tier 1. They've subscribed for 10 months!");
	CHECK(resub->comment == "hi");
	CHECK(twitch::NotificationEvent(json::parse(R"({"notice_type":"raid","system_message":"25 raiders"})"))->kind ==
	      HighlightKind::Raid);
	CHECK(twitch::NotificationEvent(
		      json::parse(R"({"notice_type":"community_sub_gift","system_message":"Giver is gifting 5"})"))
		      ->kind == HighlightKind::GiftSubs);
	CHECK_FALSE(twitch::NotificationEvent(json::parse(R"({"notice_type":"announcement","system_message":"x"})")));
	CHECK_FALSE(twitch::NotificationEvent(json::parse(R"({"notice_type":"sub"})"))); // nothing to show
}

TEST_CASE("EventSub messages carry cheers and Twitch's first-time flag")
{
	auto message = twitch::ChatMessageFromEvent(json::parse(R"({"message_id":"m1","chatter_user_login":"fan",
		"message":{"text":"Cheer100 hi"},"message_type":"user_intro","cheer":{"bits":100}})"),
						    "");
	REQUIRE(message);
	CHECK(message->bits == 100);
	CHECK(message->firstMessage);
	auto plain = twitch::ChatMessageFromEvent(json::parse(R"({"message_id":"m2","message":{"text":"hi"},
		"message_type":"text","cheer":null})"),
						  "");
	CHECK(plain->bits == 0);
	CHECK_FALSE(plain->firstMessage);
}

TEST_CASE("The chatter list learns during its first session, then spots newcomers")
{
	ChatterLog log;
	CHECK(log.Learning());
	CHECK_FALSE(log.Add(Platform::Twitch, "1")); // learning: nobody is "new" yet
	CHECK_FALSE(log.Add(Platform::YouTube, "UC1"));
	log.EndSession();
	ChatterLog next = ChatterLog::Parse(log.Serialize());
	CHECK_FALSE(next.Learning());
	CHECK(next.Size() == 2);
	CHECK_FALSE(next.Add(Platform::Twitch, "1"));
	CHECK(next.Add(Platform::Twitch, "2"));
	CHECK_FALSE(next.Add(Platform::Twitch, "2"));
	CHECK(next.Add(Platform::YouTube, "1")); // same id, other platform: someone else
	CHECK_FALSE(next.Add(Platform::Twitch, ""));

	ChatterLog empty;
	empty.EndSession(); // nobody chatted: still learning next time
	CHECK(ChatterLog::Parse(empty.Serialize()).Learning());
	CHECK(ChatterLog::Parse("garbage").Size() == 0);
}
