#include <doctest.h>

#include "core/oauth-device.hpp"

using namespace unified_chat::oauth;

TEST_CASE("Twitch device request uses scopes and no secret")
{
	auto provider = TwitchProvider(" abc ");
	CHECK(provider.clientId == "abc");
	const std::string scopes = "chat%3Aread%20chat%3Aedit%20moderator%3Amanage%3Abanned_users"
				   "%20moderator%3Amanage%3Achat_messages";
	CHECK(BuildDeviceRequestBody(provider) == "client_id=abc&scopes=" + scopes);
	CHECK(BuildDevicePollBody(provider, "dc") ==
	      "client_id=abc&scopes=" + scopes + "&device_code=dc" +
		      "&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code");
	CHECK(BuildRefreshBody(provider, "r1") == "client_id=abc&grant_type=refresh_token&refresh_token=r1");
}

TEST_CASE("Google device request uses scope and includes the secret when polling")
{
	auto provider = GoogleProvider("id", "sec");
	CHECK(BuildDeviceRequestBody(provider) ==
	      "client_id=id&scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fyoutube");
	CHECK(BuildDevicePollBody(provider, "dc").find("client_secret=sec") != std::string::npos);
	CHECK(BuildRefreshBody(provider, "r") ==
	      "client_id=id&client_secret=sec&grant_type=refresh_token&refresh_token=r");
}

TEST_CASE("ParseDeviceCode handles Twitch and Google field names")
{
	auto twitch = ParseDeviceCode(R"({"device_code":"d","expires_in":1800,"interval":5,"user_code":"ABCD",
		"verification_uri":"https://www.twitch.tv/activate?device-code=ABCD"})");
	REQUIRE(twitch);
	CHECK(twitch->userCode == "ABCD");
	CHECK(twitch->verificationUri == "https://www.twitch.tv/activate?device-code=ABCD");

	auto google = ParseDeviceCode(R"({"device_code":"d","user_code":"GQVQ-JKEC",
		"verification_url":"https://www.google.com/device","expires_in":1800,"interval":0})");
	REQUIRE(google);
	CHECK(google->verificationUri == "https://www.google.com/device");
	CHECK(google->intervalSec == 5);

	CHECK_FALSE(ParseDeviceCode(R"({"error":"invalid_client"})"));
	CHECK_FALSE(ParseDeviceCode("not json"));
}

TEST_CASE("ParseTokenResponse computes expiry and keeps refresh token")
{
	auto granted = ParseTokenResponse(200, R"({"access_token":"a","refresh_token":"r","expires_in":3600})", 1000);
	CHECK(granted.status == PollStatus::Granted);
	CHECK(granted.token.accessToken == "a");
	CHECK(granted.token.refreshToken == "r");
	CHECK(granted.token.expiresAt == 4600);

	auto refreshed = ParseTokenResponse(200, R"({"access_token":"b","expires_in":10})", 0, "old");
	CHECK(refreshed.token.refreshToken == "old");
}

TEST_CASE("ParseTokenResponse maps device flow errors for both providers")
{
	CHECK(ParseTokenResponse(428, R"({"error":"authorization_pending"})", 0).status == PollStatus::Pending);
	CHECK(ParseTokenResponse(400, R"({"status":400,"message":"authorization_pending"})", 0).status ==
	      PollStatus::Pending);
	CHECK(ParseTokenResponse(403, R"({"error":"slow_down"})", 0).status == PollStatus::SlowDown);
	CHECK(ParseTokenResponse(403, R"({"error":"access_denied"})", 0).status == PollStatus::Denied);
	CHECK(ParseTokenResponse(400, R"({"error":"expired_token"})", 0).status == PollStatus::Expired);
	CHECK(ParseTokenResponse(400, R"({"status":400,"message":"invalid device code"})", 0).status ==
	      PollStatus::Expired);

	auto bad = ParseTokenResponse(400, R"({"error":"invalid_grant"})", 0);
	CHECK(bad.status == PollStatus::Error);
	CHECK(bad.error == "invalid_grant");
	CHECK(ParseTokenResponse(500, "<html>", 0).status == PollStatus::Error);
}

TEST_CASE("Token refresh window")
{
	Token token{"a", "r", 1000};
	CHECK_FALSE(token.NeedsRefresh(900));
	CHECK(token.NeedsRefresh(941));
	Token unknown{"a", "", 0};
	CHECK_FALSE(unknown.NeedsRefresh(1'000'000));
}
