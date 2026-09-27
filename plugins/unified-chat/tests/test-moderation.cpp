#include <doctest.h>

#include "core/moderation.hpp"

using namespace unified_chat;

namespace {

ModerationAction Action(ModerationAction::Kind kind, Platform platform = Platform::Twitch)
{
	ModerationAction action;
	action.kind = kind;
	action.platform = platform;
	action.userId = platform == Platform::Twitch ? "42" : "UCdazed";
	action.messageId = platform == Platform::Twitch ? "885196de-cb67" : "LCC.abc";
	return action;
}

} // namespace

TEST_CASE("Twitch identity: user id and whether the moderation scopes were granted")
{
	auto full = ParseTwitchIdentity(R"({"client_id":"c","login":"amagestreamsyoga","user_id":"99",
		"scopes":["chat:edit","chat:read","moderator:manage:banned_users","moderator:manage:chat_messages"]})");
	REQUIRE(full);
	CHECK(full->userId == "99");
	CHECK(full->login == "amagestreamsyoga");
	CHECK(full->CanModerate());

	auto old = ParseTwitchIdentity(R"({"login":"x","user_id":"1","scopes":["chat:edit","chat:read"]})");
	REQUIRE(old);
	CHECK_FALSE(old->CanModerate()); // an older sign-in: needs signing in again

	CHECK_FALSE(ParseTwitchIdentity(R"({"status":401,"message":"invalid access token"})"));
}

TEST_CASE("Twitch moderation requests")
{
	auto del = BuildTwitchModeration(Action(ModerationAction::Kind::DeleteMessage), "71092938", "99");
	REQUIRE(del);
	CHECK(del->method == HttpRequest::Method::Delete);
	CHECK(del->url ==
	      "https://api.twitch.tv/helix/moderation/chat?broadcaster_id=71092938&moderator_id=99&message_id=885196de-cb67");

	auto timeout = Action(ModerationAction::Kind::Timeout);
	timeout.durationSeconds = 600;
	auto post = BuildTwitchModeration(timeout, "71092938", "99");
	REQUIRE(post);
	CHECK(post->method == HttpRequest::Method::Post);
	CHECK(post->url == "https://api.twitch.tv/helix/moderation/bans?broadcaster_id=71092938&moderator_id=99");
	CHECK(post->body == R"({"data":{"duration":600,"user_id":"42"}})");

	timeout.durationSeconds = 99999999; // Twitch caps timeouts at 14 days
	CHECK(BuildTwitchModeration(timeout, "1", "2")->body.find("\"duration\":1209600") != std::string::npos);

	auto ban = Action(ModerationAction::Kind::Ban);
	ban.reason = "spam links";
	CHECK(BuildTwitchModeration(ban, "1", "2")->body == R"({"data":{"reason":"spam links","user_id":"42"}})");

	auto unban = BuildTwitchModeration(Action(ModerationAction::Kind::Unban), "1", "2");
	REQUIRE(unban);
	CHECK(unban->method == HttpRequest::Method::Delete);
	CHECK(unban->url == "https://api.twitch.tv/helix/moderation/bans?broadcaster_id=1&moderator_id=2&user_id=42");

	CHECK_FALSE(BuildTwitchModeration(ban, "", "2")); // channel not known yet
	auto noMessage = Action(ModerationAction::Kind::DeleteMessage);
	noMessage.messageId.clear();
	CHECK_FALSE(BuildTwitchModeration(noMessage, "1", "2"));
}

TEST_CASE("YouTube moderation requests")
{
	auto del = BuildYouTubeModeration(Action(ModerationAction::Kind::DeleteMessage, Platform::YouTube), "CHAT1");
	REQUIRE(del);
	CHECK(del->method == HttpRequest::Method::Delete);
	CHECK(del->url == "https://www.googleapis.com/youtube/v3/liveChat/messages?id=LCC.abc");

	auto timeout = Action(ModerationAction::Kind::Timeout, Platform::YouTube);
	timeout.durationSeconds = 300;
	auto temp = BuildYouTubeModeration(timeout, "CHAT1");
	REQUIRE(temp);
	CHECK(temp->url == "https://www.googleapis.com/youtube/v3/liveChat/bans?part=snippet");
	CHECK(temp->body ==
	      R"({"snippet":{"banDurationSeconds":300,"bannedUserDetails":{"channelId":"UCdazed"},"liveChatId":"CHAT1","type":"temporary"}})");

	auto ban = BuildYouTubeModeration(Action(ModerationAction::Kind::Ban, Platform::YouTube), "CHAT1");
	REQUIRE(ban);
	CHECK(ban->body.find("\"type\":\"permanent\"") != std::string::npos);
	CHECK(ban->body.find("banDurationSeconds") == std::string::npos);

	auto unban = BuildYouTubeModeration(Action(ModerationAction::Kind::Unban, Platform::YouTube), "CHAT1", "ban-1");
	REQUIRE(unban);
	CHECK(unban->url == "https://www.googleapis.com/youtube/v3/liveChat/bans?id=ban-1");
	CHECK_FALSE(
		BuildYouTubeModeration(Action(ModerationAction::Kind::Unban, Platform::YouTube), "CHAT1")); // no ban id

	CHECK(ParseYouTubeBanId(R"({"kind":"youtube#liveChatBan","id":"ban-1","snippet":{}})") == "ban-1");
}

TEST_CASE("Moderation error messages from Twitch and Google")
{
	CHECK(ModerationErrorMessage(
		      R"({"error":"Bad Request","status":400,"message":"The user is already banned."})") ==
	      "The user is already banned.");
	CHECK(ModerationErrorMessage(R"({"error":{"code":403,"message":"The caller is not a moderator."}})") ==
	      "The caller is not a moderator.");
	CHECK(ModerationErrorMessage("").empty());
}
