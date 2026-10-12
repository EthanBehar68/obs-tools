#include <doctest.h>

#include "core/youtube-events.hpp"

using namespace unified_chat;

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

	CHECK_FALSE(ParseYouTubeEvent(R"({"kind":"jewels"})"));
	CHECK_FALSE(ParseYouTubeEvent("nope"));
	CHECK(ParseYouTubeEvent(R"({"kind":"super_chat","amount":"$5.00"})")->amount == "$5.00");
}
