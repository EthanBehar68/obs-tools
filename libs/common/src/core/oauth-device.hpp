/*
obs-unified-chat
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

#include <cstdint>
#include <optional>
#include <string>

// OAuth 2.0 Device Authorization Grant (RFC 8628) as implemented by Twitch and Google.
namespace unified_chat::oauth {

struct Provider {
	std::string deviceUrl;
	std::string tokenUrl;
	std::string clientId;
	std::string clientSecret; // Google requires it for "TVs and Limited Input" clients; Twitch public clients omit it
	std::string scope;
};

Provider TwitchProvider(const std::string &clientId);
Provider GoogleProvider(const std::string &clientId, const std::string &clientSecret);

struct DeviceCode {
	std::string deviceCode;
	std::string userCode;
	std::string verificationUri;
	int intervalSec = 5;
	int expiresInSec = 1800;
};

struct Token {
	std::string accessToken;
	std::string refreshToken;
	int64_t expiresAt = 0; // unix seconds, 0 = unknown

	bool IsValid() const { return !accessToken.empty(); }
	bool NeedsRefresh(int64_t now) const { return expiresAt != 0 && now + 60 >= expiresAt; }
};

enum class PollStatus { Granted, Pending, SlowDown, Denied, Expired, Error };

struct PollResult {
	PollStatus status = PollStatus::Error;
	Token token;
	std::string error;
};

std::string BuildDeviceRequestBody(const Provider &provider);
std::string BuildDevicePollBody(const Provider &provider, const std::string &deviceCode);
std::string BuildRefreshBody(const Provider &provider, const std::string &refreshToken);

std::optional<DeviceCode> ParseDeviceCode(const std::string &json);

// Parses a token endpoint response for both device polling and refresh requests.
// When a refresh response omits refresh_token, previousRefreshToken is kept.
PollResult ParseTokenResponse(long httpStatus, const std::string &json, int64_t now,
			      const std::string &previousRefreshToken = {});

constexpr const char *kTwitchValidateUrl = "https://id.twitch.tv/oauth2/validate";
// The login from a Twitch validate response, or an empty string.
std::string ParseTwitchLogin(const std::string &json);
// The first "reason" of a Google API error, else its message or the OAuth error code.
std::string ParseGoogleErrorReason(const std::string &json);

} // namespace unified_chat::oauth
