#include <doctest.h>

#include "core/chat-format.hpp"
#include "core/emotes.hpp"
#include "core/text-util.hpp"
#include "core/twitch-irc.hpp"

using namespace unified_chat;

namespace {

std::string Joined(const std::vector<MessageSegment> &segments)
{
	std::string out;
	for (const auto &segment : segments)
		out += segment.emote ? "[" + segment.emote->name + "]" : segment.text;
	return out;
}

} // namespace

TEST_CASE("ParseTwitchEmotes reads ids and ranges, sorted, skipping junk")
{
	auto ranges = ParseTwitchEmotes("25:12-16,0-4/1902:6-10/bad/x:1-/y:5-2");
	REQUIRE(ranges.size() == 3);
	CHECK(ranges[0].id == "25");
	CHECK(ranges[0].start == 0);
	CHECK(ranges[0].end == 4);
	CHECK(ranges[1].id == "1902");
	CHECK(ranges[2].start == 12);
	CHECK(ParseTwitchEmotes("").empty());
}

TEST_CASE("SplitMessage places Twitch emotes by character, even after emoji")
{
	// "😀 Kappa hi": the emoji is one character but four bytes; Kappa is characters 2-6.
	const std::string text = "\xF0\x9F\x98\x80 Kappa hi";
	auto segments = SplitMessage(text, {{"25", 2, 6}}, nullptr);
	CHECK(Joined(segments) == "\xF0\x9F\x98\x80 [Kappa] hi");
	REQUIRE(segments.size() == 3);
	CHECK(segments[1].emote->provider == EmoteProvider::Twitch);
	CHECK(segments[1].emote->id == "25");
}

TEST_CASE("SplitMessage ignores Twitch ranges that don't fit the text")
{
	CHECK(Joined(SplitMessage("Kappa", {{"25", 0, 9}}, nullptr)) == "Kappa"); // past the end
	CHECK(Joined(SplitMessage("Kappa Kappa", {{"25", 0, 4}, {"25", 3, 8}}, nullptr)) == "Kappa Kappa"); // overlap
}

TEST_CASE("SplitMessage finds third-party emotes as whole, case-sensitive words")
{
	EmoteIndex index;
	index.Add({{EmoteProvider::SevenTV, "7a", "catJAM", 1.0}, {EmoteProvider::BTTV, "b1", "OMEGALUL", 1.0}}, 1);
	auto segments = SplitMessage("OMEGALUL that catJAM  catjam catJAMx Kappa", {{"25", 37, 41}}, &index);
	CHECK(Joined(segments) == "[OMEGALUL] that [catJAM]  catjam catJAMx [Kappa]");
}

TEST_CASE("EmoteIndex: channel emotes beat globals, 7TV beats BTTV beats FFZ")
{
	EmoteIndex index;
	index.Add({{EmoteProvider::FFZ, "f", "LUL", 1.0}}, EmotePriority(EmoteProvider::FFZ, false));
	index.Add({{EmoteProvider::SevenTV, "s", "LUL", 1.0}}, EmotePriority(EmoteProvider::SevenTV, false));
	index.Add({{EmoteProvider::BTTV, "b", "LUL", 1.0}}, EmotePriority(EmoteProvider::BTTV, false));
	REQUIRE(index.Find("LUL"));
	CHECK(index.Find("LUL")->provider == EmoteProvider::SevenTV);

	index.Add({{EmoteProvider::FFZ, "fc", "LUL", 1.0}}, EmotePriority(EmoteProvider::FFZ, true));
	CHECK(index.Find("LUL")->id == "fc"); // any channel emote over any global
	CHECK_FALSE(index.Find("lul"));
}

TEST_CASE("Third-party emote lists parse, including shapes")
{
	auto bttvGlobal = ParseBttvEmotes(
		R"([{"id":"566ca38765dbbdab32ec0560","code":"SourPls","imageType":"gif","animated":true}])");
	REQUIRE(bttvGlobal.size() == 1);
	CHECK(bttvGlobal[0].name == "SourPls");
	CHECK(bttvGlobal[0].provider == EmoteProvider::BTTV);
	CHECK(bttvGlobal[0].animated);

	auto bttvChannel = ParseBttvEmotes(R"({"id":"x","channelEmotes":[{"id":"c1","code":"Mine"}],
		"sharedEmotes":[{"id":"s1","code":"Shared","width":56,"height":28}]})");
	REQUIRE(bttvChannel.size() == 2);
	CHECK(bttvChannel[1].aspect == doctest::Approx(2.0));

	auto ffz = ParseFfzEmotes(R"({"room":{"set":166907},"sets":{"166907":{"emoticons":[
		{"id":246878,"name":"WideHard","width":50,"height":20,"urls":{"1":"https://cdn.frankerfacez.com/emote/246878/1"}}]},
		"999":{"emoticons":[{"id":1,"name":"Other"}]}}})");
	REQUIRE(ffz.size() == 1); // only the room's set
	CHECK(ffz[0].id == "246878");
	CHECK(ffz[0].aspect == doctest::Approx(2.5));

	auto ffzGlobal = ParseFfzEmotes(R"({"default_sets":[3],"sets":{"3":{"emoticons":[{"id":9,"name":"BibleThump"}]},
		"4":{"emoticons":[{"id":8,"name":"NotDefault"}]}}})");
	REQUIRE(ffzGlobal.size() == 1);
	CHECK(ffzGlobal[0].name == "BibleThump");

	auto seventvUser = ParseSevenTvEmotes(R"({"id":"71092938","emote_set":{"emotes":[{"id":"01G3","name":"GAMBA",
		"data":{"animated":true,"host":{"url":"//cdn.7tv.app/emote/01G3","files":[{"name":"1x.webp","width":39,"height":32}]}}}]}})");
	REQUIRE(seventvUser.size() == 1);
	CHECK(seventvUser[0].name == "GAMBA");
	CHECK(seventvUser[0].aspect == doctest::Approx(39.0 / 32.0));
	CHECK(seventvUser[0].animated);

	auto seventvGlobal = ParseSevenTvEmotes(R"({"id":"global","emotes":[{"id":"01F","name":"RainTime"}]})");
	REQUIRE(seventvGlobal.size() == 1);

	CHECK(ParseBttvEmotes(R"({"message":"user not found"})").empty());
	CHECK(ParseFfzEmotes("garbage").empty());
	CHECK(ParseSevenTvEmotes(R"({"status_code":404})").empty());
}

TEST_CASE("Emote image addresses use static formats")
{
	CHECK(EmoteImageUrl({EmoteProvider::Twitch, "25", "Kappa"}, false) ==
	      "https://static-cdn.jtvnw.net/emoticons/v2/25/static/dark/1.0");
	// Animated BTTV/7TV emotes use the static file; still ones the normal address (static files exist only for
	// animated emotes; both checked against the live CDNs on 2026-09-26).
	Emote gamba{EmoteProvider::SevenTV, "01G3", "GAMBA"};
	gamba.animated = true;
	CHECK(EmoteImageUrl(gamba, true) == "https://cdn.7tv.app/emote/01G3/2x_static.webp");
	CHECK(EmoteImageUrl({EmoteProvider::SevenTV, "01P", "ppL"}, false) == "https://cdn.7tv.app/emote/01P/1x.webp");
	Emote sourPls{EmoteProvider::BTTV, "b2", "SourPls"};
	sourPls.animated = true;
	CHECK(EmoteImageUrl(sourPls, false) == "https://cdn.betterttv.net/emote/b2/static/1x.webp");
	CHECK(EmoteImageUrl({EmoteProvider::BTTV, "b1", "x"}, false) == "https://cdn.betterttv.net/emote/b1/1x");
	CHECK(EmoteImageUrl({EmoteProvider::FFZ, "9", "x"}, false) == "https://cdn.frankerfacez.com/emote/9/1");
	CHECK(EmoteImageKey({EmoteProvider::SevenTV, "01G3", "GAMBA"}) == "unified-chat://emote/s/01G3");
}

TEST_CASE("Role badges only, in a fixed order; founder replaces subscriber")
{
	auto badges = ParseRoleBadges("subscriber/12,premium/1,moderator/1,bits/1000,vip/1");
	REQUIRE(badges.size() == 3);
	CHECK(badges[0].set == "moderator");
	CHECK(badges[1].set == "vip");
	CHECK(badges[2].set == "subscriber");
	CHECK(badges[2].version == "12");

	auto founder = ParseRoleBadges("founder/0,subscriber/24");
	REQUIRE(founder.size() == 1);
	CHECK(founder[0].set == "founder");

	CHECK(ParseRoleBadges("broadcaster/1").at(0).set == "broadcaster");
	CHECK(ParseRoleBadges("").empty());
	CHECK(BadgeImageKey({"subscriber", "12"}) == "unified-chat://badge/subscriber/12");
}

TEST_CASE("Helix badge responses map set/version to an image")
{
	auto images = ParseBadgeImages(R"({"data":[{"set_id":"moderator","versions":[{"id":"1",
		"image_url_1x":"https://x/mod1","image_url_2x":"https://x/mod2","title":"Moderator"}]},
		{"set_id":"subscriber","versions":[{"id":"12","image_url_1x":"https://x/sub12"}]}]})",
				       true);
	CHECK(images["moderator/1"] == "https://x/mod2");
	CHECK(images["subscriber/12"] == "https://x/sub12"); // no 2x: falls back to 1x
	CHECK(ParseBadgeImages("{}", false).empty());
}

TEST_CASE("Twitch messages carry emotes, role badges and the channel id")
{
	twitch::IrcSession session("streamer", "", "");
	twitch::SessionOutput out;
	session.HandleLine("@room-id=71092938 :tmi.twitch.tv ROOMSTATE #streamer", out);
	CHECK(out.channelId == "71092938");

	twitch::SessionOutput again;
	session.HandleLine("@room-id=71092938 :tmi.twitch.tv ROOMSTATE #streamer", again);
	CHECK(again.channelId.empty()); // reported once

	twitch::SessionOutput message;
	session.HandleLine(
		"@badges=moderator/1,bits/100;emotes=25:0-4;id=m1 :v!v@v.tmi.twitch.tv PRIVMSG #streamer :Kappa hi",
		message);
	REQUIRE(message.messages.size() == 1);
	REQUIRE(message.messages[0].emotes.size() == 1);
	CHECK(message.messages[0].emotes[0].id == "25");
	REQUIRE(message.messages[0].badges.size() == 1);
	CHECK(message.messages[0].badges[0].set == "moderator");
}

TEST_CASE("Formatted lines show emotes and badges as images and report the rendered length")
{
	EmoteIndex index;
	index.Add({{EmoteProvider::SevenTV, "01G3", "GAMBA", 2.0}}, 1);
	ChatMessage message{Platform::Twitch, "m1", "Viewer", "Kappa GAMBA \xF0\x9F\x98\x80"};
	message.emotes = {{"25", 0, 4}};
	message.badges = {{"moderator", "1"}};

	int length = -1;
	auto html = FormatMessageHtml(message, 16, {Platform::Twitch}, nullptr, &index, 24, &length);
	CHECK(html.find("src=\"unified-chat://emote/t/25\"") != std::string::npos);
	CHECK(html.find("src=\"unified-chat://emote/s/01G3\" title=\"GAMBA\" width=\"48\" height=\"24\"") !=
	      std::string::npos);
	CHECK(html.find("src=\"unified-chat://badge/moderator/1\"") < html.find(">Viewer</span>"));
	// Kappa(1) + space + GAMBA(1) + space + emoji (a surrogate pair: 2) = 6
	CHECK(length == 6);

	// YouTube messages don't use the Twitch-side emote services.
	ChatMessage youtube{Platform::YouTube, "y1", "Someone", "GAMBA"};
	CHECK(FormatMessageHtml(youtube, 16, {Platform::YouTube}, nullptr, &index, 24).find("emote/") ==
	      std::string::npos);
}

TEST_CASE("Utf16Length counts surrogate pairs")
{
	CHECK(Utf16Length("abc") == 3);
	CHECK(Utf16Length("h\xC3\xA9") == 2);
	CHECK(Utf16Length("\xF0\x9F\x98\x80") == 2);
}
