#include <doctest.h>

#include "core/alert-queue.hpp"
#include "core/alerts-config.hpp"

using namespace stream_alerts;

static AlertEvent Follow(int n)
{
	return {AlertKind::Follow, "id" + std::to_string(n), "user" + std::to_string(n)};
}

TEST_CASE("Alerts play in order, once per person per stream")
{
	AlertQueue queue;
	CHECK(queue.Push(Follow(1)));
	CHECK(queue.Push({AlertKind::Subscriber, "id1", "Sub"})); // same id, other platform: its own alert
	CHECK_FALSE(queue.Push(Follow(1)));                       // unfollow + refollow
	auto first = queue.Pop();
	REQUIRE(first);
	CHECK(first->names == std::vector<std::string>{"user1"});
	CHECK_FALSE(first->combined);
	CHECK(queue.Pop()->kind == AlertKind::Subscriber);
	CHECK_FALSE(queue.Pop());

	CHECK_FALSE(queue.Push(Follow(1))); // still the same stream
	queue.Reset();
	CHECK(queue.Push(Follow(1))); // a new stream
}

TEST_CASE("Past 10 waiting, a kind's alerts combine into one at the end")
{
	AlertQueue queue;
	for (int n = 1; n <= 25; ++n)
		CHECK(queue.Push(Follow(n)));
	CHECK(queue.Push({AlertKind::Subscriber, "s1", "Sub1"})); // after the combined follows, and combined too
	CHECK(queue.Size() == AlertQueue::kMaxQueued + 2);

	for (int n = 1; n <= 10; ++n) {
		auto alert = queue.Pop();
		REQUIRE(alert);
		CHECK_FALSE(alert->combined);
		CHECK(alert->names.front() == "user" + std::to_string(n));
	}
	// Slots are free again, but later follows still join the combined alert so the order stays right.
	CHECK(queue.Push(Follow(26)));
	auto combined = queue.Pop();
	REQUIRE(combined);
	CHECK(combined->combined);
	CHECK(combined->kind == AlertKind::Follow);
	CHECK(combined->names.size() == 16);
	CHECK(combined->names.back() == "user26");
	auto subscriber = queue.Pop();
	REQUIRE(subscriber);
	CHECK(subscriber->combined);
	CHECK(queue.Empty());
}

TEST_CASE("Alert messages fill in the name, or the count and as many names as fit")
{
	const AlertsConfig config;
	CHECK(FormatAlert({AlertKind::Follow, {"Cool_User"}, false}, config.follow.message,
			  config.follow.overflowMessage) == "Cool_User just followed!");

	Alert many{AlertKind::Follow, {"aa", "bb", "cc"}, true};
	CHECK(FormatAlert(many, config.follow.message, config.follow.overflowMessage) ==
	      "+3 more new followers!\naa, bb, cc");
	CHECK(FormatAlert(many, config.follow.message, config.follow.overflowMessage, 6) ==
	      "+3 more new followers!\naa, bb, …");
	CHECK(FormatAlert(many, "{name}", "{count} {count}", 1) == "3 3\n…");
}

TEST_CASE("Alerts config round trips and falls back to defaults")
{
	AlertsConfig config;
	config.follow.enabled = false;
	config.follow.source = "Follow Alert";
	config.follow.textSource = "Follow Text";
	config.subscriber.message = "{name} subbed";
	config.durationSeconds = 8;
	AlertsConfig loaded = ParseAlertsConfig(SerializeAlertsConfig(config));
	CHECK_FALSE(loaded.follow.enabled);
	CHECK(loaded.follow.source == "Follow Alert");
	CHECK(loaded.follow.textSource == "Follow Text");
	CHECK(loaded.subscriber.message == "{name} subbed");
	CHECK(loaded.subscriber.overflowMessage == "+{count} more new subscribers!");
	CHECK(loaded.durationSeconds == 8);

	AlertsConfig damaged = ParseAlertsConfig(R"({"follow":{"enabled":"yes","source":3},"duration_seconds":999})");
	CHECK(damaged.follow.enabled);
	CHECK(damaged.follow.source.empty());
	CHECK(damaged.durationSeconds == 60);
	CHECK(ParseAlertsConfig("not json").follow.message == "{name} just followed!");
}
