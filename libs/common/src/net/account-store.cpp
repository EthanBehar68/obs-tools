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

#include "account-store.hpp"
#include "curl-http-client.hpp"
#include "secret-store.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <algorithm>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

namespace obs_tools {

#ifdef _WIN32
// Local\ = this Windows session: every plugin in every OBS instance the user runs.
static constexpr const wchar_t *kFileLock = L"Local\\obs-tools-accounts-file";
static constexpr const wchar_t *kRefreshLock = L"Local\\obs-tools-accounts-refresh";

class NamedLock {
public:
	// With cancel, waits in short slices and gives up once it's set, so closing OBS never waits out timeoutMs
	// behind another plugin.
	NamedLock(const wchar_t *name, DWORD timeoutMs, const std::atomic<bool> *cancel = nullptr)
	{
		handle_ = CreateMutexW(nullptr, FALSE, name);
		if (!handle_)
			return;
		constexpr DWORD kSliceMs = 100;
		for (DWORD waited = 0;; waited += kSliceMs) {
			const DWORD slice = cancel ? (std::min)(kSliceMs, timeoutMs - (std::min)(waited, timeoutMs))
						   : timeoutMs;
			DWORD result = WaitForSingleObject(handle_, slice);
			if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) { // abandoned: the holder crashed
				held_ = true;
				return;
			}
			if (result != WAIT_TIMEOUT || !cancel || cancel->load() || waited + slice >= timeoutMs)
				return;
		}
	}
	~NamedLock()
	{
		if (held_)
			ReleaseMutex(handle_);
		if (handle_)
			CloseHandle(handle_);
	}
	NamedLock(const NamedLock &) = delete;
	NamedLock &operator=(const NamedLock &) = delete;

	bool Held() const { return held_; }

private:
	HANDLE handle_ = nullptr;
	bool held_ = false;
};
#else
static constexpr const wchar_t *kFileLock = L"";
static constexpr const wchar_t *kRefreshLock = L"";

class NamedLock {
public:
	NamedLock(const wchar_t *, unsigned long, const std::atomic<bool> * = nullptr) {}
	bool Held() const { return true; }
};
#endif

// File access takes milliseconds; a refresh is one HTTPS request.
static constexpr unsigned long kFileLockMs = 5000;
static constexpr unsigned long kRefreshLockMs = 30000;

static const SecretCodec *Codec()
{
	return PlatformSecretCodec(SecretPurpose::Accounts);
}

static std::string ReadText(const std::string &path)
{
	char *text = os_quick_read_utf8_file(path.c_str());
	std::string out = text ? text : "";
	bfree(text);
	return out;
}

static Accounts ReadFile(const std::string &path, SecretReport *report)
{
	return ParseAccounts(ReadText(path), Codec(), report);
}

AccountStore::AccountStore(std::string dir) : dir_(std::move(dir)), path_(dir_ + "/accounts.json") {}

Accounts AccountStore::Load(SecretReport *report, const std::atomic<bool> *cancel) const
{
	NamedLock lock(kFileLock, kFileLockMs,
		       cancel); // not held after a timeout: reading anyway beats blocking a sign-in
	return ReadFile(path_, report);
}

void AccountStore::Update(const std::function<void(Accounts &)> &change, const std::atomic<bool> *cancel)
{
	// Written even when cancelled: it may hold a refreshed Twitch token, and the old refresh token is spent.
	NamedLock lock(kFileLock, kFileLockMs, cancel);
	if (!lock.Held())
		obs_log(LOG_WARNING, "accounts file lock %s, writing anyway",
			cancel && cancel->load() ? "wait cut short by shutdown" : "timed out");
	const std::string previous = ReadText(path_);
	Accounts accounts = ParseAccounts(previous, Codec());
	change(accounts);
	os_mkdirs(dir_.c_str());
	std::string text = SerializeAccounts(accounts, Codec(), previous);
	if (!os_quick_write_utf8_file_safe(path_.c_str(), text.c_str(), text.size(), false, "tmp", "bak"))
		obs_log(LOG_WARNING, "failed to save %s", path_.c_str());
}

bool AccountStore::ReplaceIfCurrent(Service service, const oauth::Token &stale, const oauth::Token &next,
				    const std::atomic<bool> *cancel)
{
	bool replaced = false;
	Update(
		[&](Accounts &accounts) {
			oauth::Token &stored = accounts.Get(service).token;
			if (SameToken(stored, stale)) {
				stored = next;
				replaced = true;
			}
		},
		cancel);
	return replaced;
}

AccountStore::RefreshResult AccountStore::Refresh(Service service, const oauth::Token &stale,
						  const std::atomic<bool> *cancel)
{
	using oauth::RefreshOutcome;
	// Held across the request: a Twitch refresh token works once, so a second plugin must wait and then take
	// the token this one saved instead of spending the same refresh token.
	NamedLock refreshing(kRefreshLock, kRefreshLockMs, cancel);
	if (!refreshing.Held())
		return {RefreshOutcome::Transient, {}};

	const Account stored = Load(nullptr, cancel).Get(service);
	switch (NextRefreshStep(stored.token, stale)) {
	case RefreshStep::UseStored:
		return {RefreshOutcome::Granted, stored.token};
	case RefreshStep::SignedOut:
		return {RefreshOutcome::Rejected, {}};
	case RefreshStep::Refresh:
		break;
	}

	const oauth::Provider provider = service == Service::Twitch
						 ? oauth::TwitchProvider(stored.clientId)
						 : oauth::GoogleProvider(stored.clientId, stored.clientSecret);
	if (provider.clientId.empty())
		return {RefreshOutcome::Rejected, {}};
	CurlHttpClient http(cancel);
	auto res = http.Post(provider.tokenUrl, {}, oauth::BuildRefreshBody(provider, stored.token.refreshToken),
			     "application/x-www-form-urlencoded");
	auto parsed =
		oauth::ParseTokenResponse(res.status, res.body, (int64_t)std::time(nullptr), stored.token.refreshToken);
	const RefreshOutcome outcome = oauth::ClassifyRefresh(res.status, parsed);
	if (outcome != RefreshOutcome::Granted) {
		const bool badClient = parsed.error.find("client") != std::string::npos; // invalid_client, ...
		obs_log(LOG_WARNING, "%s sign-in refresh %s (%s)%s", service == Service::Twitch ? "Twitch" : "Google",
			outcome == RefreshOutcome::Rejected ? "rejected" : "failed for now", parsed.error.c_str(),
			badClient ? ": the client ID (or secret) in Tools > OBS Tools > Accounts isn't accepted" : "");
		return {outcome, {}};
	}
	if (!ReplaceIfCurrent(service, stored.token, parsed.token, cancel))
		return {RefreshOutcome::Granted,
			Load(nullptr, cancel).Get(service).token}; // the user signed in again meanwhile: it wins
	return {RefreshOutcome::Granted, parsed.token};
}

void AccountStore::Invalidate(Service service, const oauth::Token &stale)
{
	if (stale.IsValid())
		ReplaceIfCurrent(service, stale, {});
}

void AccountStore::Replace(Service service, const oauth::Token &stale, const oauth::Token &next)
{
	ReplaceIfCurrent(service, stale, next);
}

AccountStore &SharedAccounts()
{
	static AccountStore store([] {
		char *own = obs_module_config_path("");
		std::string dir = own ? own : "";
		bfree(own);
		while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
			dir.pop_back();
		auto slash = dir.find_last_of("/\\");
		return (slash == std::string::npos ? std::string() : dir.substr(0, slash + 1)) + "obs-tools";
	}());
	return store;
}

} // namespace obs_tools
