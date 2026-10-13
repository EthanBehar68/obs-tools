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

TEST_CASE("Paid alerts and raids go ahead of waiting follows, every time, and never combine")
{
	AlertQueue queue;
	for (int n = 1; n <= 12; ++n)
		queue.Push(Follow(n)); // 10 individual + 1 combined
	AlertEvent cheer{AlertKind::Cheer, "msg1", "Cheerer"};
	cheer.count = 100;
	CHECK(queue.Push(cheer));
	CHECK_FALSE(queue.Push(cheer)); // the same event sent twice
	cheer.id = "msg2";
	CHECK(queue.Push(cheer)); // the same person cheering again: alerts again
	CHECK(queue.Push({AlertKind::Raid, "r1", "Raider"}));
	for (int n = 0; n < 20; ++n)
		queue.Push({AlertKind::SuperChat, "c" + std::to_string(n), "Fan"}); // never limited or combined

	auto first = queue.Pop();
	REQUIRE(first);
	CHECK(first->kind == AlertKind::Cheer);
	CHECK(first->event.count == 100);
	CHECK(queue.Pop()->kind == AlertKind::Cheer);
	CHECK(queue.Pop()->kind == AlertKind::Raid); // in arrival order among themselves
	for (int n = 0; n < 20; ++n) {
		auto superChat = queue.Pop();
		REQUIRE(superChat);
		CHECK(superChat->kind == AlertKind::SuperChat);
		CHECK_FALSE(superChat->combined);
	}
	CHECK(queue.Pop()->names.front() == "user1"); // then the follows, as before
}

TEST_CASE("Alert messages fill in each kind's details")
{
	const AlertsConfig config;
	AlertEvent resub{AlertKind::TwitchSub, "1", "Fan"};
	resub.variant = true;
	resub.months = 14;
	resub.message = "{name} love it"; // a viewer's text is never expanded
	CHECK(FormatAlert({resub.kind, {resub.name}, false, resub}, config.twitchSub.variantMessage + " {message}",
			  {}) == "Fan resubscribed for 14 months! {name} love it");

	AlertEvent raid{AlertKind::Raid, "2", "Pal"};
	raid.viewers = 25;
	CHECK(FormatAlert({raid.kind, {raid.name}, false, raid}, config.raid.message, {}) ==
	      "Pal is raiding with 25 viewers!");

	AlertEvent superChat{AlertKind::SuperChat, "3", "@viewer"};
	superChat.amount = "$5.00";
	superChat.message = "great stream";
	CHECK(FormatAlert({superChat.kind, {superChat.name}, false, superChat}, config.superChat.message, {}) ==
	      "@viewer sent $5.00!\ngreat stream");

	AlertEvent jewels{AlertKind::Jewels, "4#3", "@fan"};
	jewels.gift = "Rose";
	jewels.count = 3;
	CHECK(FormatAlert({jewels.kind, {jewels.name}, false, jewels}, config.jewels.message, {}) == "@fan sent Rose!");
	CHECK(FormatAlert({jewels.kind, {jewels.name}, false, jewels}, config.jewels.variantMessage, {}) ==
	      "@fan sent 3 × Rose!");
}

TEST_CASE("A kind is set up only with something to show")
{
	AlertsConfig config;
	CHECK_FALSE(config.cheer.IsSetUp());
	config.cheer.textSource = "Cheer Text";
	CHECK(config.cheer.IsSetUp());
	config.cheer.enabled = false;
	CHECK_FALSE(config.cheer.IsSetUp());
}

TEST_CASE("Every kind, the minimums and variant messages round trip")
{
	AlertsConfig config;
	for (AlertKind kind : kAllKinds)
		config.For(kind).source = std::string("Source ") + KindKey(kind);
	config.member.variantMessage = "{months}!";
	config.minBits = 500;
	config.minRaidViewers = 10;
	config.minJewels = 50;
	config.subscriberCheckSeconds = 300;
	AlertsConfig loaded = ParseAlertsConfig(SerializeAlertsConfig(config));
	for (AlertKind kind : kAllKinds)
		CHECK(loaded.For(kind).source == std::string("Source ") + KindKey(kind));
	CHECK(loaded.member.variantMessage == "{months}!");
	CHECK(loaded.minBits == 500);
	CHECK(loaded.minRaidViewers == 10);
	CHECK(loaded.minJewels == 50);
	CHECK(loaded.subscriberCheckSeconds == 300);
	CHECK(ParseAlertsConfig(R"({"subscriber_check_seconds":5})").subscriberCheckSeconds == 30);

	// A 0.1.0 config: only follow and subscriber; the new kinds get their defaults.
	AlertsConfig old = ParseAlertsConfig(R"({"follow":{"source":"F"},"subscriber":{"source":"S"}})");
	CHECK(old.follow.source == "F");
	CHECK(old.giftSubs.message == "{name} gifted {count} subs!");
	CHECK(old.minBits == 1);
	CHECK(old.minJewels == 1);
	CHECK(old.jewels.message == "{name} sent {gift}!");
	CHECK(old.subscriberCheckSeconds == 120);
	CHECK(ParseAlertsConfig(R"({"min_bits":0})").minBits == 1);
}

TEST_CASE("Raising a minimum drops waiting cheers below it, nothing else")
{
	AlertQueue queue;
	auto cheer = [](const char *id, int64_t bits) {
		AlertEvent event{AlertKind::Cheer, id, "Fan"};
		event.count = bits;
		return event;
	};
	queue.Push(Follow(1));
	queue.Push(cheer("a", 1));
	queue.Push(cheer("b", 500));
	queue.Push(cheer("c", 5));
	CHECK(queue.RemoveIf([](const Alert &alert) {
		return alert.kind == AlertKind::Cheer && alert.event.count < 100;
	}) == 2);
	CHECK(queue.Pop()->event.count == 500);
	CHECK(queue.Pop()->kind == AlertKind::Follow);
	CHECK(queue.Empty());
	for (int n = 2; n <= 20; ++n)
		queue.Push(Follow(n)); // removing kept the individual count right: still 10, then combined
	CHECK(queue.Size() == AlertQueue::kMaxQueued + 1);
}
