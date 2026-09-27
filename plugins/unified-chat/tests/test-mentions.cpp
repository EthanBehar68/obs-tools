#include <doctest.h>

#include "core/mentions.hpp"

using namespace unified_chat;

TEST_CASE("Mention links round trip, including characters that need encoding")
{
	auto url = BuildMentionLink(Platform::YouTube, "Jebbins.Gaming-1", "LCC.abc/def==");
	CHECK(url.find("unified-chat://mention/youtube/") == 0);
	auto link = ParseMentionLink(url);
	REQUIRE(link);
	CHECK(link->platform == Platform::YouTube);
	CHECK(link->mention == "Jebbins.Gaming-1");
	CHECK(link->messageId == "LCC.abc/def==");

	auto twitch = ParseMentionLink(BuildMentionLink(Platform::Twitch, "dezad", ""));
	REQUIRE(twitch);
	CHECK(twitch->platform == Platform::Twitch);
	CHECK(twitch->messageId.empty());

	CHECK_FALSE(ParseMentionLink("https://example.com/"));
	CHECK_FALSE(ParseMentionLink("unified-chat://mention/facebook/x/1"));
	CHECK_FALSE(ParseMentionLink("unified-chat://mention/twitch//1"));
}

TEST_CASE("MentionMatcher finds whole-word mentions with or without @")
{
	MentionMatcher matcher;
	CHECK(matcher.Empty());
	CHECK_FALSE(matcher.Matches("hi amagestreamsyoga"));

	matcher.SetNames({"amagestreamsyoga", "@aMageStreamsYoga", "", "Mage Channel"});
	CHECK(matcher.Matches("@aMageStreamsYoga you here?"));
	CHECK(matcher.Matches("hey AMAGESTREAMSYOGA!"));
	CHECK(matcher.Matches("thanks @amagestreamsyoga"));
	CHECK(matcher.Matches("love the Mage Channel stream"));
	CHECK_FALSE(matcher.Matches("amagestreamsyoga2 is someone else"));
	CHECK_FALSE(matcher.Matches("xamagestreamsyoga"));
	CHECK_FALSE(matcher.Matches(""));
}

TEST_CASE("RecentChatters completes most recent first, per platform")
{
	RecentChatters chatters;
	chatters.Add(Platform::Twitch, "dezad");
	chatters.Add(Platform::YouTube, "Jebbins");
	chatters.Add(Platform::Twitch, "jebbinsTTV");
	chatters.Add(Platform::Twitch, "Dezad"); // seen again: moves to the front, keeps the new spelling

	auto all = chatters.Complete("@");
	REQUIRE(all.size() == 3);
	CHECK(all[0].mention == "Dezad");
	CHECK(all[1].mention == "jebbinsTTV");
	CHECK(all[2].mention == "Jebbins");

	auto jeb = chatters.Complete("jeb");
	REQUIRE(jeb.size() == 2);
	CHECK(jeb[0].platform == Platform::Twitch);

	auto youtube = chatters.Complete("JEB", Platform::YouTube);
	REQUIRE(youtube.size() == 1);
	CHECK(youtube[0].mention == "Jebbins");

	chatters.Add(Platform::Twitch, "");
	CHECK(chatters.Size() == 3);
}

TEST_CASE("RecentChatters forgets the oldest chatter past its limit")
{
	RecentChatters chatters;
	for (size_t i = 0; i <= RecentChatters::kMaxEntries; ++i)
		chatters.Add(Platform::Twitch, "user" + std::to_string(i));
	CHECK(chatters.Size() == RecentChatters::kMaxEntries);
	CHECK(chatters.Complete("user0", Platform::Twitch).empty());
	CHECK(chatters.Complete("user1", Platform::Twitch, 1).at(0).mention.rfind("user1", 0) == 0);
}
