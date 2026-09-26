#include <doctest.h>

#include "core/chat-config.hpp"
#include "core/chat-format.hpp"
#include "core/chat-router.hpp"

using namespace unified_chat;

TEST_CASE("PlanSend routes to the selected platforms")
{
	auto both = PlanSend(SendTarget::Both, "  hello  ", true, true);
	CHECK(both.text == "hello");
	REQUIRE(both.targets.size() == 2);
	CHECK(both.targets[0] == Platform::Twitch);
	CHECK(both.targets[1] == Platform::YouTube);
	CHECK(both.errors.empty());
	CHECK_FALSE(both.blocked);

	auto twitch = PlanSend(SendTarget::Twitch, "hi", true, true);
	REQUIRE(twitch.targets.size() == 1);
	CHECK(twitch.targets[0] == Platform::Twitch);

	auto youtube = PlanSend(SendTarget::YouTube, "hi", true, true);
	REQUIRE(youtube.targets.size() == 1);
	CHECK(youtube.targets[0] == Platform::YouTube);
}

TEST_CASE("PlanSend skips disconnected platforms but still sends to the others")
{
	auto plan = PlanSend(SendTarget::Both, "hi", true, false);
	REQUIRE(plan.targets.size() == 1);
	CHECK(plan.targets[0] == Platform::Twitch);
	REQUIRE(plan.errors.size() == 1);
	CHECK(plan.errors[0].rfind("YouTube", 0) == 0);
	CHECK_FALSE(plan.blocked);

	auto none = PlanSend(SendTarget::YouTube, "hi", true, false);
	CHECK(none.targets.empty());
	CHECK(none.blocked);
}

TEST_CASE("PlanSend blocks messages over a platform limit")
{
	std::string text(201, 'a');
	auto plan = PlanSend(SendTarget::Both, text, true, true);
	CHECK(plan.blocked);
	CHECK(plan.targets.empty());
	REQUIRE(plan.errors.size() == 1);
	CHECK(plan.errors[0] == "YouTube: message is 201 characters, the limit is 200");

	auto twitchOnly = PlanSend(SendTarget::Twitch, text, true, true);
	CHECK_FALSE(twitchOnly.blocked);

	std::string emoji;
	for (int i = 0; i < 200; ++i)
		emoji += "\xF0\x9F\x98\x80";
	CHECK_FALSE(PlanSend(SendTarget::YouTube, emoji, true, true).blocked); // limit counts characters, not bytes
}

TEST_CASE("PlanSend ignores empty input")
{
	auto plan = PlanSend(SendTarget::Both, " \r\n ", true, true);
	CHECK(plan.blocked);
	CHECK(plan.targets.empty());
	CHECK(plan.errors.empty());
}

TEST_CASE("MentionTarget switches per mention and restores the chosen target")
{
	MentionTarget target;
	CHECK_FALSE(target.Pending());
	CHECK(target.Home(SendTarget::Both) == SendTarget::Both);

	// On Both, Tab cycles dazed263 (YouTube) -> dezad (Twitch) -> duxclarus (Twitch): each switches.
	SendTarget current = SendTarget::Both;
	current = target.Switch(current, Platform::YouTube);
	CHECK(current == SendTarget::YouTube);
	CHECK(target.Home(current) == SendTarget::Both); // still Both, so the next pick may switch again
	current = target.Switch(current, Platform::Twitch);
	CHECK(current == SendTarget::Twitch);
	current = target.Switch(current, Platform::Twitch);
	CHECK(current == SendTarget::Twitch);
	CHECK(target.Home(current) == SendTarget::Both);

	// Sending restores Both, once.
	CHECK(target.Restore() == SendTarget::Both);
	CHECK_FALSE(target.Pending());
	CHECK_FALSE(target.Restore());
}

TEST_CASE("MentionTarget keeps a target the user picked by hand")
{
	MentionTarget target;
	SendTarget current = target.Switch(SendTarget::Twitch, Platform::YouTube);
	CHECK(current == SendTarget::YouTube);
	target.Forget(); // the user clicked the switch mid-mention
	CHECK_FALSE(target.Restore());
	CHECK(target.Home(SendTarget::YouTube) == SendTarget::YouTube);
}

TEST_CASE("SendTarget string round trip")
{
	for (auto target : {SendTarget::Twitch, SendTarget::YouTube, SendTarget::Both})
		CHECK(SendTargetFromString(SendTargetToString(target)) == target);
	CHECK(SendTargetFromString("garbage") == SendTarget::Both);
}

TEST_CASE("FormatMessageHtml shows icon, name and message in order")
{
	ChatMessage msg{Platform::Twitch, "1", "Viewer", "hello", "#112233"};
	auto html = FormatMessageHtml(msg, 16);
	auto icon = html.find(kTwitchIconResource);
	auto name = html.find(">Viewer</span>");
	auto text = html.find(": <span style=\"white-space: pre-wrap;\">hello</span>");
	CHECK(icon != std::string::npos);
	CHECK(name != std::string::npos);
	CHECK(text != std::string::npos);
	CHECK(icon < name);
	CHECK(name < text);
	CHECK(html.find("color: #112233") != std::string::npos);
	CHECK(html.find("width=\"16\"") != std::string::npos);
}

TEST_CASE("FormatMessageHtml escapes user content and ignores unsafe colors")
{
	ChatMessage msg{Platform::YouTube, "1", "<b>evil</b>", "<img src=http://x>", "red;background:url(x)"};
	auto html = FormatMessageHtml(msg, 18);
	CHECK(html.find(kYouTubeIconResource) != std::string::npos);
	CHECK(html.find("<b>") == std::string::npos);
	CHECK(html.find("<img src=http") == std::string::npos);
	CHECK(html.find("&lt;img src=http://x&gt;") != std::string::npos);
	CHECK(html.find(std::string(DefaultNameColor(Platform::YouTube))) != std::string::npos);
	CHECK(html.find("background") == std::string::npos);
}

TEST_CASE("FormatMessageHtml renders /me actions without a colon")
{
	ChatMessage msg{Platform::Twitch, "1", "A", "waves", "", true};
	auto html = FormatMessageHtml(msg, 16);
	CHECK(html.find(": <span") == std::string::npos);
	CHECK(html.find("font-style: italic; white-space: pre-wrap;\">waves") != std::string::npos);
}

TEST_CASE("FormatMessageHtml puts a star after the platform icons on your own lines")
{
	ChatMessage mine{Platform::Twitch, "1", "me", "hi all"};
	mine.isSelf = true;
	auto html = FormatMessageHtml(mine, 16, {Platform::Twitch, Platform::YouTube});
	auto twitch = html.find(kTwitchIconResource);
	auto youtube = html.find(kYouTubeIconResource);
	auto star = html.find(kSelfBadgeResource);
	auto name = html.find(">me</span>");
	REQUIRE(star != std::string::npos);
	CHECK(twitch < youtube);
	CHECK(youtube < star);
	CHECK(star < name);

	ChatMessage theirs{Platform::Twitch, "2", "viewer", "hi"};
	CHECK(FormatMessageHtml(theirs, 16).find(kSelfBadgeResource) == std::string::npos);
}

TEST_CASE("Other people's names are mention links; your own isn't")
{
	ChatMessage viewer{Platform::Twitch, "m1", "Dezad", "hi"};
	viewer.mention = "dezad";
	auto html = FormatMessageHtml(viewer, 16);
	CHECK(html.find("<a href=\"unified-chat://mention/twitch/dezad/m1\"") != std::string::npos);
	CHECK(html.find(">Dezad</span></a>") != std::string::npos);

	ChatMessage mine = viewer;
	mine.isSelf = true;
	CHECK(FormatMessageHtml(mine, 16).find("<a ") == std::string::npos);

	ChatMessage noMention{Platform::YouTube, "y1", "Someone", "hi"};
	CHECK(FormatMessageHtml(noMention, 16).find("<a ") == std::string::npos);
}

TEST_CASE("Replies show who they answer between the name and the text")
{
	ChatMessage reply{Platform::Twitch, "m2", "Viewer", "welcome back"};
	reply.mention = "viewer";
	reply.replyTo = "<Dezad>";
	auto html = FormatMessageHtml(reply, 16);
	auto name = html.find(">Viewer</span>");
	auto marker = html.find("\xE2\x86\x92 @&lt;Dezad&gt;");
	auto text = html.find(">welcome back</span>");
	REQUIRE(marker != std::string::npos);
	CHECK(name < marker);
	CHECK(marker < text);
}

TEST_CASE("Moderation tags and notices")
{
	ModerationEvent deleted;
	deleted.kind = ModerationEvent::Kind::DeleteMessage;
	CHECK(TagFor(deleted).label == "(deleted)");
	CHECK(TagFor(deleted).severity == 1);
	CHECK(ModerationNotice(deleted, "dezad").empty());

	ModerationEvent timeout;
	timeout.kind = ModerationEvent::Kind::RemoveUser;
	timeout.durationSeconds = 600;
	CHECK(TagFor(timeout).label == "(timed out 10 minutes)");
	CHECK(TagFor(timeout).color == std::string(kTagTimeoutColor));
	CHECK(ModerationNotice(timeout, "trollguy") == "Twitch: trollguy was timed out for 10 minutes");

	ModerationEvent ban;
	ban.platform = Platform::YouTube;
	ban.kind = ModerationEvent::Kind::RemoveUser;
	CHECK(TagFor(ban).label == "(banned)");
	CHECK(ModerationNotice(ban, "@dazed263") == "YouTube: @dazed263 was banned");

	ModerationEvent clear;
	clear.kind = ModerationEvent::Kind::ClearChat;
	CHECK(TagFor(clear).label == "(chat cleared)");
	CHECK(ModerationNotice(clear, "") == "Twitch: chat was cleared by a moderator");

	// Precedence: banned > timed out > deleted / cleared.
	CHECK(TagFor(ban).severity > TagFor(timeout).severity);
	CHECK(TagFor(timeout).severity > TagFor(deleted).severity);
	CHECK(TagFor(clear).severity == TagFor(deleted).severity);
}

TEST_CASE("FormatNoticeHtml escapes")
{
	CHECK(FormatNoticeHtml("<x>").find("&lt;x&gt;") != std::string::npos);
}

TEST_CASE("Config round trips through JSON")
{
	ChatConfig config;
	config.twitchChannel = "streamer";
	config.twitchClientId = "tc";
	config.twitchLogin = "me";
	config.twitchToken = {"ta", "tr", 123};
	config.youtubeClientId = "yc";
	config.youtubeClientSecret = "ys";
	config.youtubeVideo = "dQw4w9WgXcQ";
	config.youtubeToken = {"ya", "yr", 456};
	config.youtubePollSeconds = 12;
	config.youtubeStream = false;
	config.youtubeConnectOnStream = false;
	config.sendTarget = SendTarget::YouTube;
	config.maxMessages = 800;

	ChatConfig loaded = ParseConfig(SerializeConfig(config));
	CHECK(loaded.twitchChannel == "streamer");
	CHECK(loaded.twitchClientId == "tc");
	CHECK(loaded.twitchLogin == "me");
	CHECK(loaded.twitchToken.accessToken == "ta");
	CHECK(loaded.twitchToken.refreshToken == "tr");
	CHECK(loaded.twitchToken.expiresAt == 123);
	CHECK(loaded.youtubeClientId == "yc");
	CHECK(loaded.youtubeClientSecret == "ys");
	CHECK(loaded.youtubeVideo == "dQw4w9WgXcQ");
	CHECK(loaded.youtubeToken.expiresAt == 456);
	CHECK(loaded.youtubePollSeconds == 12);
	CHECK_FALSE(loaded.youtubeStream);
	CHECK(ParseConfig("{}").youtubeStream); // streaming is the default
	CHECK_FALSE(loaded.youtubeConnectOnStream);
	CHECK(ParseConfig("{}").youtubeConnectOnStream); // waiting for Start Streaming is the default
	CHECK(loaded.sendTarget == SendTarget::YouTube);
	CHECK(loaded.maxMessages == 800);
}

namespace {

// Stands in for DPAPI: "sealed:" + reversed text; refuses anything it didn't seal.
SecretCodec FakeCodec()
{
	return {[](const std::string &plain) -> std::optional<std::string> {
			return "sealed:" + std::string(plain.rbegin(), plain.rend());
		},
		[](const std::string &sealed) -> std::optional<std::string> {
			if (sealed.rfind("sealed:", 0) != 0)
				return std::nullopt;
			std::string body = sealed.substr(7);
			return std::string(body.rbegin(), body.rend());
		}};
}

ChatConfig SignedIn()
{
	ChatConfig config;
	config.twitchClientId = "public-twitch-id";
	config.twitchToken = {"twitch-access", "twitch-refresh", 123};
	config.youtubeClientId = "public-google-id";
	config.youtubeClientSecret = "google-secret";
	config.youtubeToken = {"yt-access", "yt-refresh", 456};
	return config;
}

} // namespace

TEST_CASE("Secrets are stored encrypted and read back")
{
	const SecretCodec codec = FakeCodec();
	const std::string text = SerializeConfig(SignedIn(), &codec);
	for (const char *secret : {"twitch-access", "twitch-refresh", "google-secret", "yt-access", "yt-refresh"}) {
		CAPTURE(secret);
		CHECK(text.find(secret) == std::string::npos);
	}
	CHECK(text.find("enc:v1:") != std::string::npos);
	CHECK(text.find("public-twitch-id") != std::string::npos); // client IDs aren't secret

	ParseReport report;
	ChatConfig loaded = ParseConfig(text, &codec, &report);
	CHECK(loaded.twitchToken.accessToken == "twitch-access");
	CHECK(loaded.twitchToken.refreshToken == "twitch-refresh");
	CHECK(loaded.youtubeClientSecret == "google-secret");
	CHECK(loaded.youtubeToken.refreshToken == "yt-refresh");
	CHECK(loaded.youtubeToken.expiresAt == 456);
	CHECK_FALSE(report.plaintextSecrets);
	CHECK_FALSE(report.unreadableSecrets);
}

TEST_CASE("An older plain-text config still loads and is reported for re-saving")
{
	const SecretCodec codec = FakeCodec();
	ParseReport report;
	ChatConfig loaded = ParseConfig(SerializeConfig(SignedIn()), &codec, &report);
	CHECK(loaded.twitchToken.accessToken == "twitch-access");
	CHECK(report.plaintextSecrets);
	CHECK_FALSE(report.unreadableSecrets);
}

TEST_CASE("Secrets that can't be decrypted come back empty instead of failing")
{
	const SecretCodec codec = FakeCodec();
	std::string text = SerializeConfig(SignedIn(), &codec);

	ParseReport noCodec;
	ChatConfig withoutCodec = ParseConfig(text, nullptr, &noCodec);
	CHECK(withoutCodec.twitchToken.accessToken.empty());
	CHECK(withoutCodec.youtubeClientSecret.empty());
	CHECK(withoutCodec.twitchClientId == "public-twitch-id"); // everything else still loads
	CHECK(noCodec.unreadableSecrets);

	SecretCodec otherAccount = codec;
	otherAccount.unprotect = [](const std::string &) -> std::optional<std::string> {
		return std::nullopt;
	};
	ParseReport report;
	CHECK_FALSE(ParseConfig(text, &otherAccount, &report).twitchToken.IsValid());
	CHECK(report.unreadableSecrets);
}

TEST_CASE("If encryption fails, secrets are kept rather than lost")
{
	SecretCodec broken = FakeCodec();
	broken.protect = [](const std::string &) -> std::optional<std::string> {
		return std::nullopt;
	};
	ChatConfig loaded = ParseConfig(SerializeConfig(SignedIn(), &broken), &broken);
	CHECK(loaded.twitchToken.accessToken == "twitch-access");
}

TEST_CASE("Config falls back to defaults for damaged input")
{
	ChatConfig empty = ParseConfig("{not json");
	CHECK(empty.sendTarget == SendTarget::Both);
	CHECK(empty.youtubePollSeconds == 8);
	CHECK(empty.maxMessages == 500);

	ChatConfig wrongTypes = ParseConfig(R"({"twitch":{"channel":5},"youtube":{"poll_seconds":0},
		"max_messages":"lots","send_target":"twitch"})");
	CHECK(wrongTypes.twitchChannel.empty());
	CHECK(wrongTypes.youtubePollSeconds == 1);
	CHECK(wrongTypes.maxMessages == 500);
	CHECK(wrongTypes.sendTarget == SendTarget::Twitch);
}
