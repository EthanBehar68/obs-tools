#include <doctest.h>

#include "core/youtube-events.hpp"

using namespace obs_tools;

TEST_CASE("YouTube events survive the trip between plugins")
{
	YouTubeEvent event;
	event.kind = YouTubeEvent::Kind::MemberMilestone;
	event.id = "LCC.abc";
	event.channelId = "UC1";
	event.name = "@viewer";
	event.level = "Gold";
	event.months = 12;
	event.message = "a year already";
	auto parsed = ParseYouTubeEvent(SerializeYouTubeEvent(event));
	REQUIRE(parsed);
	CHECK(parsed->kind == YouTubeEvent::Kind::MemberMilestone);
	CHECK(parsed->id == "LCC.abc");
	CHECK(parsed->name == "@viewer");
	CHECK(parsed->level == "Gold");
	CHECK(parsed->months == 12);
	CHECK(parsed->message == "a year already");

	CHECK_FALSE(ParseYouTubeEvent(R"({"kind":"fireworks"})")); // a kind from a newer release
	CHECK_FALSE(ParseYouTubeEvent("nope"));
	CHECK(ParseYouTubeEvent(R"({"kind":"super_chat","amount":"$5.00"})")->amount == "$5.00");
}

TEST_CASE("A Jewels gift carries its gift, combo and value")
{
	YouTubeEvent event;
	event.kind = YouTubeEvent::Kind::Jewels;
	event.id = "LCC.gift#3";
	event.gift = "Rose";
	event.combo = 3;
	event.count = 2;
	event.jewels = 10;
	auto parsed = ParseYouTubeEvent(SerializeYouTubeEvent(event));
	REQUIRE(parsed);
	CHECK(parsed->kind == YouTubeEvent::Kind::Jewels);
	CHECK(parsed->id == "LCC.gift#3");
	CHECK(parsed->gift == "Rose");
	CHECK(parsed->combo == 3);
	CHECK(parsed->count == 2);
	CHECK(JewelsTotal(*parsed) == 20);
}
