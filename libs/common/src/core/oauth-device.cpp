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

#include "oauth-device.hpp"
#include "text-util.hpp"

#include <json.hpp>

using json = nlohmann::json;

namespace unified_chat::oauth {

static constexpr const char *kDeviceGrant = "urn:ietf:params:oauth:grant-type:device_code";

Provider TwitchProvider(const std::string &clientId)
{
	// One sign-in serves every plugin, so it asks for all of their scopes: chat and moderating from the chat
	// dock (unified-chat), follower alerts (stream-alerts).
	return {"https://id.twitch.tv/oauth2/device",
		"https://id.twitch.tv/oauth2/token",
		Trim(clientId),
		{},
		"chat:read chat:edit moderator:manage:banned_users moderator:manage:chat_messages "
		"moderator:read:followers"};
}

Provider GoogleProvider(const std::string &clientId, const std::string &clientSecret)
{
	return {"https://oauth2.googleapis.com/device/code", "https://oauth2.googleapis.com/token", Trim(clientId),
		Trim(clientSecret), "https://www.googleapis.com/auth/youtube"};
}

static bool IsTwitch(const Provider &provider)
{
	return provider.tokenUrl.find("twitch.tv") != std::string::npos;
}

static void AddClient(const Provider &provider, std::vector<std::pair<std::string, std::string>> &fields)
{
	fields.emplace_back("client_id", provider.clientId);
	if (!provider.clientSecret.empty())
		fields.emplace_back("client_secret", provider.clientSecret);
}

std::string BuildDeviceRequestBody(const Provider &provider)
{
	std::vector<std::pair<std::string, std::string>> fields;
	fields.emplace_back("client_id", provider.clientId);
	fields.emplace_back(IsTwitch(provider) ? "scopes" : "scope", provider.scope);
	return FormEncode(fields);
}

std::string BuildDevicePollBody(const Provider &provider, const std::string &deviceCode)
{
	std::vector<std::pair<std::string, std::string>> fields;
	AddClient(provider, fields);
	if (IsTwitch(provider))
		fields.emplace_back("scopes", provider.scope);
	fields.emplace_back("device_code", deviceCode);
	fields.emplace_back("grant_type", kDeviceGrant);
	return FormEncode(fields);
}

std::string BuildRefreshBody(const Provider &provider, const std::string &refreshToken)
{
	std::vector<std::pair<std::string, std::string>> fields;
	AddClient(provider, fields);
	fields.emplace_back("grant_type", "refresh_token");
	fields.emplace_back("refresh_token", refreshToken);
	return FormEncode(fields);
}

static std::string StringField(const json &obj, const char *key)
{
	auto it = obj.find(key);
	return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static int IntField(const json &obj, const char *key, int fallback)
{
	auto it = obj.find(key);
	return it != obj.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

std::optional<DeviceCode> ParseDeviceCode(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return std::nullopt;

	DeviceCode code;
	code.deviceCode = StringField(obj, "device_code");
	code.userCode = StringField(obj, "user_code");
	// Twitch uses verification_uri, Google uses verification_url
	code.verificationUri = StringField(obj, "verification_uri");
	if (code.verificationUri.empty())
		code.verificationUri = StringField(obj, "verification_url");
	code.intervalSec = IntField(obj, "interval", 5);
	code.expiresInSec = IntField(obj, "expires_in", 1800);

	if (code.deviceCode.empty() || code.userCode.empty() || code.verificationUri.empty())
		return std::nullopt;
	if (code.intervalSec < 1)
		code.intervalSec = 5;
	return code;
}

PollResult ParseTokenResponse(long httpStatus, const std::string &body, int64_t now,
			      const std::string &previousRefreshToken)
{
	PollResult result;
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object()) {
		result.error = "HTTP " + std::to_string(httpStatus) + ": invalid response";
		return result;
	}

	if (httpStatus >= 200 && httpStatus < 300 && obj.contains("access_token")) {
		result.status = PollStatus::Granted;
		result.token.accessToken = StringField(obj, "access_token");
		result.token.refreshToken = StringField(obj, "refresh_token");
		if (result.token.refreshToken.empty())
			result.token.refreshToken = previousRefreshToken;
		int expiresIn = IntField(obj, "expires_in", 0);
		result.token.expiresAt = expiresIn > 0 ? now + expiresIn : 0;
		if (result.token.accessToken.empty())
			result.status = PollStatus::Error;
		return result;
	}

	// Google: {"error": "authorization_pending"}, Twitch: {"status": 400, "message": "authorization_pending"}
	std::string code = StringField(obj, "error");
	if (code.empty() || code == "Bad Request")
		code = StringField(obj, "message");
	result.error = code.empty() ? "HTTP " + std::to_string(httpStatus) : code;

	if (code == "authorization_pending")
		result.status = PollStatus::Pending;
	else if (code == "slow_down")
		result.status = PollStatus::SlowDown;
	else if (code == "access_denied")
		result.status = PollStatus::Denied;
	else if (code == "expired_token" || code == "invalid device code")
		result.status = PollStatus::Expired;
	else
		result.status = PollStatus::Error;
	return result;
}

std::string ParseTwitchLogin(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	return obj.is_object() ? StringField(obj, "login") : std::string();
}

std::string ParseGoogleErrorReason(const std::string &body)
{
	json obj = json::parse(body, nullptr, false);
	if (!obj.is_object())
		return {};
	auto error = obj.find("error");
	if (error == obj.end())
		return {};
	if (error->is_string())
		return error->get<std::string>();
	if (!error->is_object())
		return {};
	auto errors = error->find("errors");
	if (errors != error->end() && errors->is_array() && !errors->empty() && (*errors)[0].is_object()) {
		std::string reason = StringField((*errors)[0], "reason");
		if (!reason.empty())
			return reason;
	}
	return StringField(*error, "message");
}

} // namespace unified_chat::oauth
