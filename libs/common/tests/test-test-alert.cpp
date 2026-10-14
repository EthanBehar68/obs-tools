#include <doctest.h>

#include "core/test-alert.hpp"

using namespace obs_tools;

TEST_CASE("Test alerts round trip between the plugins")
{
	const auto back = ParseTestAlert(SerializeTestAlert({"super_chat", false, "Fan sent $5.00: \"hi\""}));
	REQUIRE(back);
	CHECK(back->kind == "super_chat");
	CHECK_FALSE(back->twitch);
	CHECK(back->text == "Fan sent $5.00: \"hi\"");

	const auto twitch = ParseTestAlert(SerializeTestAlert({"twitch_sub", true, "TestSubscriber just subscribed!"}));
	REQUIRE(twitch);
	CHECK(twitch->twitch);
}

TEST_CASE("Malformed or newer test alerts are ignored")
{
	CHECK_FALSE(ParseTestAlert("not json"));
	CHECK_FALSE(ParseTestAlert(R"({"v":2,"kind":"raid","text":"x"})"));
	CHECK_FALSE(ParseTestAlert(R"({"v":1,"text":"x"})"));
	CHECK_FALSE(ParseTestAlert(R"({"v":1,"kind":"raid","text":5})"));
}
