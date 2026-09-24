#include <doctest.h>

#include "core/chat-format.hpp"
#include "core/echo-merger.hpp"

using namespace unified_chat;

namespace {

ChatMessage Echo(Platform platform, const std::string &author, uint64_t sendId)
{
	ChatMessage message;
	message.platform = platform;
	message.author = author;
	message.text = "hello both";
	message.isSelf = true;
	message.sendId = sendId;
	return message;
}

const std::vector<Platform> kBoth{Platform::Twitch, Platform::YouTube};

} // namespace

TEST_CASE("Echoes from both platforms become one line")
{
	EchoMerger merger;
	uint64_t id = merger.Begin(kBoth, 0);

	CHECK(merger.Offer(Echo(Platform::Twitch, "Streamer", id)).empty());
	CHECK(merger.HasPending());

	auto lines = merger.Offer(Echo(Platform::YouTube, "streamer", id));
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].platforms == kBoth);
	CHECK(lines[0].message.author == "Streamer"); // same name, different case: shown once
	CHECK(lines[0].message.text == "hello both");
	CHECK_FALSE(merger.HasPending());
}

TEST_CASE("Different account names are both shown, Twitch first even if YouTube echoes first")
{
	EchoMerger merger;
	uint64_t id = merger.Begin(kBoth, 0);
	CHECK(merger.Offer(Echo(Platform::YouTube, "My Channel", id)).empty());
	auto lines = merger.Offer(Echo(Platform::Twitch, "mytwitch", id));
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].platforms == kBoth);
	CHECK(lines[0].message.platform == Platform::Twitch);
	CHECK(lines[0].message.author == "mytwitch / My Channel");
}

TEST_CASE("A failed platform is dropped from the line")
{
	EchoMerger merger;
	uint64_t id = merger.Begin(kBoth, 0);
	CHECK(merger.Offer(Echo(Platform::Twitch, "me", id)).empty());
	auto lines = merger.Fail(id, Platform::YouTube);
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].platforms == std::vector<Platform>{Platform::Twitch});
}

TEST_CASE("Nothing is shown when every platform fails")
{
	EchoMerger merger;
	uint64_t id = merger.Begin(kBoth, 0);
	CHECK(merger.Fail(id, Platform::Twitch).empty());
	CHECK(merger.Fail(id, Platform::YouTube).empty());
	CHECK_FALSE(merger.HasPending());
}

TEST_CASE("Timeout releases whatever arrived, and late echoes pass through")
{
	EchoMerger merger(5000);
	uint64_t id = merger.Begin(kBoth, 1000);
	CHECK(merger.Offer(Echo(Platform::Twitch, "me", id)).empty());
	CHECK(merger.Expire(5999).empty());

	auto expired = merger.Expire(6000);
	REQUIRE(expired.size() == 1);
	CHECK(expired[0].platforms == std::vector<Platform>{Platform::Twitch});
	CHECK_FALSE(merger.HasPending());

	auto late = merger.Offer(Echo(Platform::YouTube, "me", id));
	REQUIRE(late.size() == 1);
	CHECK(late[0].platforms == std::vector<Platform>{Platform::YouTube});
}

TEST_CASE("Viewer messages and single-platform sends pass straight through")
{
	EchoMerger merger;
	ChatMessage viewer;
	viewer.platform = Platform::YouTube;
	viewer.author = "viewer";
	auto lines = merger.Offer(viewer);
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].platforms == std::vector<Platform>{Platform::YouTube});

	uint64_t id = merger.Begin({Platform::Twitch}, 0);
	auto single = merger.Offer(Echo(Platform::Twitch, "me", id));
	REQUIRE(single.size() == 1);
	CHECK(single[0].platforms == std::vector<Platform>{Platform::Twitch});
}

TEST_CASE("Concurrent sends are merged independently")
{
	EchoMerger merger;
	uint64_t first = merger.Begin(kBoth, 0);
	uint64_t second = merger.Begin(kBoth, 0);
	CHECK(first != second);
	CHECK(merger.Offer(Echo(Platform::Twitch, "me", first)).empty());
	CHECK(merger.Offer(Echo(Platform::Twitch, "me", second)).empty());
	CHECK(merger.Offer(Echo(Platform::YouTube, "me", second)).size() == 1);
	CHECK(merger.HasPending());
	CHECK(merger.Offer(Echo(Platform::YouTube, "me", first)).size() == 1);
}

TEST_CASE("FormatMessageHtml renders one icon per platform before the name")
{
	ChatMessage message = Echo(Platform::Twitch, "me", 1);
	auto html = FormatMessageHtml(message, 16, kBoth);
	auto twitch = html.find(kTwitchIconResource);
	auto youtube = html.find(kYouTubeIconResource);
	auto name = html.find(">me</span>");
	REQUIRE(twitch != std::string::npos);
	REQUIRE(youtube != std::string::npos);
	CHECK(twitch < youtube);
	CHECK(youtube < name);
	CHECK(FormatMessageHtml(message, 16) == FormatMessageHtml(message, 16, {Platform::Twitch}));
}
