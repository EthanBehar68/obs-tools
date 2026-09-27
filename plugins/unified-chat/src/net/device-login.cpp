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

#include "device-login.hpp"
#include "net/curl-http-client.hpp"
#include "core/twitch-irc.hpp"
#include "core/youtube-api.hpp"

#include <chrono>
#include <ctime>

namespace unified_chat {

static constexpr const char *kFormType = "application/x-www-form-urlencoded";

DeviceLogin::DeviceLogin(oauth::Provider provider, Callbacks callbacks)
	: provider_(std::move(provider)),
	  callbacks_(std::move(callbacks))
{
	thread_ = std::thread(&DeviceLogin::Run, this);
}

DeviceLogin::~DeviceLogin()
{
	stop_ = true;
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

void DeviceLogin::WaitFor(int seconds)
{
	std::unique_lock lock(mutex_);
	cv_.wait_for(lock, std::chrono::seconds(seconds), [this] { return stop_.load(); });
}

void DeviceLogin::Run()
{
	auto finish = [this](const oauth::Token &token, const std::string &login, const std::string &error) {
		if (!stop_ && callbacks_.onFinished)
			callbacks_.onFinished(token, login, error);
	};

	CurlHttpClient http(&stop_);
	auto res = http.Post(provider_.deviceUrl, {}, oauth::BuildDeviceRequestBody(provider_), kFormType);
	auto code = res.Ok() ? oauth::ParseDeviceCode(res.body) : std::nullopt;
	if (!code) {
		std::string reason = youtube::ParseErrorReason(res.body);
		if (reason.empty())
			reason = res.status == 0 ? res.error : "HTTP " + std::to_string(res.status);
		finish({}, {}, "could not start sign-in (" + reason + "). Check the client ID");
		return;
	}
	if (callbacks_.onCode)
		callbacks_.onCode(*code);

	int interval = code->intervalSec;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(code->expiresInSec);
	while (!stop_) {
		WaitFor(interval);
		if (stop_)
			return;
		if (std::chrono::steady_clock::now() > deadline) {
			finish({}, {}, "the sign-in code expired, try again");
			return;
		}

		res = http.Post(provider_.tokenUrl, {}, oauth::BuildDevicePollBody(provider_, code->deviceCode),
				kFormType);
		if (res.status == 0)
			continue;
		auto poll = oauth::ParseTokenResponse(res.status, res.body, (int64_t)std::time(nullptr));
		switch (poll.status) {
		case oauth::PollStatus::Pending:
			break;
		case oauth::PollStatus::SlowDown:
			interval += 5;
			break;
		case oauth::PollStatus::Granted: {
			std::string login;
			if (provider_.tokenUrl.find("twitch.tv") != std::string::npos) {
				auto validate = http.Get(twitch::kValidateUrl,
							 {"Authorization: OAuth " + poll.token.accessToken});
				login = twitch::ParseValidateLogin(validate.body);
				if (login.empty()) {
					finish({}, {}, "signed in, but Twitch did not return the account name");
					return;
				}
			}
			finish(poll.token, login, {});
			return;
		}
		case oauth::PollStatus::Denied:
			finish({}, {}, "sign-in was denied");
			return;
		case oauth::PollStatus::Expired:
			finish({}, {}, "the sign-in code expired, try again");
			return;
		default:
			finish({}, {}, "sign-in failed (" + poll.error + ")");
			return;
		}
	}
}

} // namespace unified_chat
