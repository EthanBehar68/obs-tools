#include <doctest.h>

#include "core/accounts.hpp"

using namespace unified_chat;

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

Accounts SignedIn()
{
	Accounts accounts;
	accounts.twitch.clientId = "public-twitch-id";
	accounts.twitch.login = "me";
	accounts.twitch.token = {"twitch-access", "twitch-refresh", 123};
	accounts.google.clientId = "public-google-id";
	accounts.google.clientSecret = "google-secret";
	accounts.google.token = {"yt-access", "yt-refresh", 456};
	return accounts;
}

} // namespace

TEST_CASE("Accounts round trip through JSON")
{
	Accounts loaded = ParseAccounts(SerializeAccounts(SignedIn()));
	CHECK(loaded.twitch.clientId == "public-twitch-id");
	CHECK(loaded.twitch.login == "me");
	CHECK(loaded.twitch.token.accessToken == "twitch-access");
	CHECK(loaded.twitch.token.refreshToken == "twitch-refresh");
	CHECK(loaded.twitch.token.expiresAt == 123);
	CHECK(loaded.google.clientId == "public-google-id");
	CHECK(loaded.google.clientSecret == "google-secret");
	CHECK(loaded.google.token.refreshToken == "yt-refresh");
	CHECK(loaded.google.token.expiresAt == 456);

	CHECK(ParseAccounts("").twitch.Empty());
	CHECK(ParseAccounts(R"({"twitch":[],"google":{"token":7}})").google.Empty());
}

TEST_CASE("Account secrets are stored encrypted and read back")
{
	const SecretCodec codec = FakeCodec();
	const std::string text = SerializeAccounts(SignedIn(), &codec);
	for (const char *secret : {"twitch-access", "twitch-refresh", "google-secret", "yt-access", "yt-refresh"}) {
		CAPTURE(secret);
		CHECK(text.find(secret) == std::string::npos);
	}
	CHECK(text.find("enc:v1:") != std::string::npos);
	CHECK(text.find("public-twitch-id") != std::string::npos); // client IDs aren't secret

	SecretReport report;
	Accounts loaded = ParseAccounts(text, &codec, &report);
	CHECK(loaded.twitch.token.accessToken == "twitch-access");
	CHECK(loaded.google.clientSecret == "google-secret");
	CHECK(loaded.google.token.refreshToken == "yt-refresh");
	CHECK_FALSE(report.plaintextSecrets);
	CHECK_FALSE(report.unreadableSecrets);

	SecretReport plain;
	CHECK(ParseAccounts(SerializeAccounts(SignedIn()), &codec, &plain).twitch.token.IsValid());
	CHECK(plain.plaintextSecrets);
}

TEST_CASE("Account secrets that can't be decrypted come back empty")
{
	const SecretCodec codec = FakeCodec();
	const std::string text = SerializeAccounts(SignedIn(), &codec);
	SecretCodec otherAccount = codec;
	otherAccount.unprotect = [](const std::string &) -> std::optional<std::string> {
		return std::nullopt;
	};
	SecretReport report;
	Accounts loaded = ParseAccounts(text, &otherAccount, &report);
	CHECK_FALSE(loaded.twitch.token.IsValid());
	CHECK(loaded.google.clientSecret.empty());
	CHECK(loaded.twitch.clientId == "public-twitch-id"); // everything else still loads
	CHECK(report.unreadableSecrets);
}

TEST_CASE("If encryption fails, account secrets are kept rather than lost")
{
	SecretCodec broken = FakeCodec();
	broken.protect = [](const std::string &) -> std::optional<std::string> {
		return std::nullopt;
	};
	CHECK(ParseAccounts(SerializeAccounts(SignedIn(), &broken), &broken).twitch.token.accessToken ==
	      "twitch-access");
}

TEST_CASE("A refresh spends the stored token only when it is the stale one")
{
	const oauth::Token stale{"a1", "r1", 100};
	CHECK(NextRefreshStep(stale, stale) == RefreshStep::Refresh);
	// The other plugin refreshed first: r1 is spent, a2 is the one to use.
	CHECK(NextRefreshStep({"a2", "r2", 200}, stale) == RefreshStep::UseStored);
	CHECK(NextRefreshStep({}, stale) == RefreshStep::SignedOut);              // signed out meanwhile
	CHECK(NextRefreshStep({"a1", "", 100}, stale) == RefreshStep::SignedOut); // nothing to refresh with
}

TEST_CASE("Legacy sign-ins fill only services the shared accounts don't have")
{
	Accounts shared;
	shared.twitch.clientId = "shared-twitch";
	Accounts legacy = SignedIn();
	CHECK(AdoptLegacyAccounts(shared, legacy));
	CHECK(shared.twitch.clientId == "shared-twitch"); // already set up: left alone
	CHECK_FALSE(shared.twitch.token.IsValid());
	CHECK(shared.google.token.accessToken == "yt-access");
	CHECK_FALSE(AdoptLegacyAccounts(shared, legacy)); // nothing left to copy
	CHECK(SameToken(shared.google.token, legacy.google.token));
}
