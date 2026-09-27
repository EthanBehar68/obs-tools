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

#pragma once

#include "oauth-device.hpp"
#include "secret-codec.hpp"

#include <string>

// The sign-ins shared by every obs-tools plugin (plugin_config/obs-tools/accounts.json).
namespace unified_chat {

enum class Service { Twitch, Google };

struct Account {
	std::string clientId;
	std::string clientSecret; // Google only
	std::string login;        // Twitch only: the signed-in account's login name
	oauth::Token token;

	bool Empty() const { return clientId.empty() && clientSecret.empty() && !token.IsValid(); }
};

struct Accounts {
	Account twitch;
	Account google;

	Account &Get(Service service) { return service == Service::Twitch ? twitch : google; }
	const Account &Get(Service service) const { return service == Service::Twitch ? twitch : google; }
};

// Tokens and the Google client secret are sealed with the codec when one is given.
std::string SerializeAccounts(const Accounts &accounts, const SecretCodec *codec = nullptr);
// Missing or malformed fields come back empty, so a damaged file only means signing in again.
Accounts ParseAccounts(const std::string &json, const SecretCodec *codec = nullptr, SecretReport *report = nullptr);

bool SameToken(const oauth::Token &a, const oauth::Token &b);

enum class RefreshStep {
	UseStored, // another plugin already refreshed (or the user signed in again): take the stored token
	Refresh,   // the stored token is the stale one: refresh it
	SignedOut, // nothing to refresh with
};
// stale: the token a caller found expired or rejected; stored: what the shared file holds now. Twitch refresh
// tokens are single-use, so only the plugin still holding the stored token may spend it.
RefreshStep NextRefreshStep(const oauth::Token &stored, const oauth::Token &stale);

// Fills services the shared file doesn't have yet from an older per-plugin config. True if anything was copied.
bool AdoptLegacyAccounts(Accounts &shared, const Accounts &legacy);

} // namespace unified_chat
