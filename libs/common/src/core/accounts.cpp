/*
obs-tools
Copyright (C) 2026 ebehar

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "accounts.hpp"

#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat {

static std::string GetString(const json &obj, const char *key)
{
	auto it = obj.find(key);
	return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static json TokenToJson(const oauth::Token &token, const SecretCodec *codec)
{
	return {{"access_token", SealSecret(token.accessToken, codec)},
		{"refresh_token", SealSecret(token.refreshToken, codec)},
		{"expires_at", token.expiresAt}};
}

static oauth::Token TokenFromJson(const json &obj, const SecretCodec *codec, SecretReport *report)
{
	oauth::Token token;
	auto it = obj.find("token");
	if (it == obj.end() || !it->is_object())
		return token;
	token.accessToken = OpenSecret(GetString(*it, "access_token"), codec, report);
	token.refreshToken = OpenSecret(GetString(*it, "refresh_token"), codec, report);
	auto expires = it->find("expires_at");
	token.expiresAt = expires != it->end() && expires->is_number_integer() ? expires->get<int64_t>() : 0;
	return token;
}

std::string SerializeAccounts(const Accounts &accounts, const SecretCodec *codec)
{
	json obj = {
		{"version", 1},
		{"twitch",
		 {{"client_id", accounts.twitch.clientId},
		  {"login", accounts.twitch.login},
		  {"token", TokenToJson(accounts.twitch.token, codec)}}},
		{"google",
		 {{"client_id", accounts.google.clientId},
		  {"client_secret", SealSecret(accounts.google.clientSecret, codec)},
		  {"token", TokenToJson(accounts.google.token, codec)}}},
	};
	return obj.dump(4);
}

Accounts ParseAccounts(const std::string &text, const SecretCodec *codec, SecretReport *report)
{
	Accounts accounts;
	json obj = json::parse(text, nullptr, false);
	if (!obj.is_object())
		return accounts;

	auto twitch = obj.find("twitch");
	if (twitch != obj.end() && twitch->is_object()) {
		accounts.twitch.clientId = GetString(*twitch, "client_id");
		accounts.twitch.login = GetString(*twitch, "login");
		accounts.twitch.token = TokenFromJson(*twitch, codec, report);
	}
	auto google = obj.find("google");
	if (google != obj.end() && google->is_object()) {
		accounts.google.clientId = GetString(*google, "client_id");
		accounts.google.clientSecret = OpenSecret(GetString(*google, "client_secret"), codec, report);
		accounts.google.token = TokenFromJson(*google, codec, report);
	}
	return accounts;
}

bool SameToken(const oauth::Token &a, const oauth::Token &b)
{
	return a.accessToken == b.accessToken && a.refreshToken == b.refreshToken;
}

RefreshStep NextRefreshStep(const oauth::Token &stored, const oauth::Token &stale)
{
	if (!stored.IsValid())
		return RefreshStep::SignedOut;
	if (stored.accessToken != stale.accessToken)
		return RefreshStep::UseStored;
	return stored.refreshToken.empty() ? RefreshStep::SignedOut : RefreshStep::Refresh;
}

bool AdoptLegacyAccounts(Accounts &shared, const Accounts &legacy)
{
	bool copied = false;
	for (Service service : {Service::Twitch, Service::Google}) {
		if (shared.Get(service).Empty() && !legacy.Get(service).Empty()) {
			shared.Get(service) = legacy.Get(service);
			copied = true;
		}
	}
	return copied;
}

} // namespace unified_chat
