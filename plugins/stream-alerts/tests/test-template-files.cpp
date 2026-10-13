#include <doctest.h>

#include "core/template-files.hpp"

#include <set>

using namespace stream_alerts;

TEST_CASE("Alert template: images and videos are told apart by extension")
{
	CHECK(IsImageFile("C:\\Alerts\\follow.png"));
	CHECK(IsImageFile("C:\\Alerts\\hype.GIF"));
	CHECK(IsImageFile("D:/alerts/star.webp"));
	CHECK_FALSE(IsImageFile("C:\\Alerts\\follow.mp4"));
	CHECK_FALSE(IsImageFile("C:\\Alerts\\follow.webm"));
	CHECK_FALSE(IsImageFile("C:\\my.folder\\follow"));
	CHECK_FALSE(IsImageFile(""));
}

TEST_CASE("Alert template: names are made unique the way OBS does")
{
	std::set<std::string> taken{"Twitch Follow Alert", "Twitch Follow Alert 2"};
	auto isTaken = [&](const std::string &name) {
		return taken.count(name) > 0;
	};
	CHECK(UniqueName("Twitch Follow Alert", isTaken) == "Twitch Follow Alert 3");
	CHECK(UniqueName("Twitch Raid Alert", isTaken) == "Twitch Raid Alert");
}
