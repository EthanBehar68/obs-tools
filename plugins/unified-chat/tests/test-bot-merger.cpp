#include <doctest.h>

#include "core/bot-merger.hpp"
#include "core/chat-config.hpp"

using namespace unified_chat;

namespace {

ChatMessage Line(Platform platform, const std::string &author, const std::string &text)
{
	ChatMessage message;
	message.platform = platform;
	message.author = author;
	message.text = text;
	return message;
}

BotMerger Nightbot()
{
	BotMerger merger;
	merger.SetBots({"Nightbot"});
	return merger;
}

} // namespace

TEST_CASE("IsBot ignores case, spaces and a leading @")
{
	BotMerger merger;
	merger.SetBots({" Nightbot ", "@StreamElements", ""});
	CHECK(merger.IsBot("Nightbot"));
	CHECK(merger.IsBot("nightbot"));
	CHECK(merger.IsBot("@Nightbot"));
	CHECK(merger.IsBot("streamelements"));
	CHECK_FALSE(merger.IsBot("Nightbot2"));
	CHECK_FALSE(merger.IsBot(""));

	BotMerger none;
	none.SetBots({});
	CHECK_FALSE(none.IsBot("Nightbot"));
}

TEST_CASE("The YouTube copy of a Twitch bot line merges into that line")
{
	BotMerger merger = Nightbot();
	auto twitch = Line(Platform::Twitch, "Nightbot", "Join the Discord!");
	CHECK_FALSE(merger.Match(twitch, 0));
	merger.Remember(twitch, 7, 0);

	auto merged = merger.Match(Line(Platform::YouTube, "@Nightbot", " Join the Discord! "), 9000);
	REQUIRE(merged);
	CHECK(merged->lineId == 7);
	CHECK(merged->line.platforms == std::vector<Platform>{Platform::Twitch, Platform::YouTube});
	CHECK(merged->line.message.platform == Platform::Twitch); // Twitch copy's name and color
	CHECK(merger.RecentCount() == 0);

	// A third copy is a new line, not another merge.
	CHECK_FALSE(merger.Match(Line(Platform::YouTube, "Nightbot", "Join the Discord!"), 9500));
}

TEST_CASE("A YouTube-first bot line still shows Twitch first once merged")
{
	BotMerger merger = Nightbot();
	auto youtube = Line(Platform::YouTube, "Nightbot", "Hydrate!");
	merger.Remember(youtube, 3, 0);
	auto merged = merger.Match(Line(Platform::Twitch, "Nightbot", "Hydrate!"), 1000);
	REQUIRE(merged);
	CHECK(merged->lineId == 3);
	CHECK(merged->line.platforms.front() == Platform::Twitch);
	CHECK(merged->line.message.platform == Platform::Twitch);
}

TEST_CASE("Bot lines don't merge on the same platform, with different text, or after the window")
{
	BotMerger merger = Nightbot();
	merger.Remember(Line(Platform::Twitch, "Nightbot", "Hydrate!"), 1, 0);

	CHECK_FALSE(merger.Match(Line(Platform::Twitch, "Nightbot", "Hydrate!"), 1000)); // timer repeat
	CHECK_FALSE(merger.Match(Line(Platform::YouTube, "Nightbot", "Stretch!"), 1000));
	CHECK(merger.RecentCount() == 1);

	CHECK_FALSE(merger.Match(Line(Platform::YouTube, "Nightbot", "Hydrate!"), BotMerger::kWindowMs + 1));
	CHECK(merger.RecentCount() == 0);
}

TEST_CASE("The recent list is bounded and cleared when the bot list changes")
{
	BotMerger merger = Nightbot();
	for (int i = 0; i < (int)BotMerger::kMaxRecent + 10; ++i)
		merger.Remember(Line(Platform::Twitch, "Nightbot", "line " + std::to_string(i)), i, 0);
	CHECK(merger.RecentCount() == BotMerger::kMaxRecent);

	merger.SetBots({"Nightbot", "Moobot"});
	CHECK(merger.RecentCount() == 0);
}

TEST_CASE("Bot name lists split and join for the settings field")
{
	CHECK(SplitNameList(" Nightbot, StreamElements ,,") == std::vector<std::string>{"Nightbot", "StreamElements"});
	CHECK(SplitNameList("").empty());
	CHECK(JoinNameList({"Nightbot", "Moobot"}) == "Nightbot, Moobot");
	CHECK(JoinNameList({}) == "");
}

TEST_CASE("Merge bots default to Nightbot and round trip, including an emptied list")
{
	CHECK(ParseConfig("{}").mergeBots == std::vector<std::string>{"Nightbot"});

	ChatConfig config;
	config.mergeBots = {"Nightbot", "Fossabot"};
	CHECK(ParseConfig(SerializeConfig(config)).mergeBots == config.mergeBots);

	config.mergeBots.clear();
	CHECK(ParseConfig(SerializeConfig(config)).mergeBots.empty());
}
