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

#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

namespace unified_chat {

#ifdef _WIN32
// Local\ = this Windows session: every plugin in every OBS instance the user runs.
static constexpr const wchar_t *kFileLock = L"Local\\obs-tools-accounts-file";
static constexpr const wchar_t *kRefreshLock = L"Local\\obs-tools-accounts-refresh";

class NamedLock {
public:
	NamedLock(const wchar_t *name, DWORD timeoutMs)
	{
		handle_ = CreateMutexW(nullptr, FALSE, name);
		if (handle_) {
			DWORD result = WaitForSingleObject(handle_, timeoutMs);
			held_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED; // abandoned: the holder crashed
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
	NamedLock(const wchar_t *, unsigned long) {}
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

static Accounts ReadFile(const std::string &path, SecretReport *report)
{
	char *text = os_quick_read_utf8_file(path.c_str());
	Accounts accounts = ParseAccounts(text ? text : "", Codec(), report);
	bfree(text);
	return accounts;
}

AccountStore::AccountStore(std::string dir) : dir_(std::move(dir)), path_(dir_ + "/accounts.json") {}

Accounts AccountStore::Load(SecretReport *report) const
{
	NamedLock lock(kFileLock, kFileLockMs); // not held after a timeout: reading anyway beats blocking a sign-in
	return ReadFile(path_, report);
}

void AccountStore::Update(const std::function<void(Accounts &)> &change)
{
	NamedLock lock(kFileLock, kFileLockMs);
	if (!lock.Held())
		obs_log(LOG_WARNING, "accounts file lock timed out, writing anyway");
	Accounts accounts = ReadFile(path_, nullptr);
	change(accounts);
	os_mkdirs(dir_.c_str());
	std::string text = SerializeAccounts(accounts, Codec());
	if (!os_quick_write_utf8_file_safe(path_.c_str(), text.c_str(), text.size(), false, "tmp", "bak"))
		obs_log(LOG_WARNING, "failed to save %s", path_.c_str());
}

bool AccountStore::ReplaceIfCurrent(Service service, const oauth::Token &stale, const oauth::Token &next)
{
	bool replaced = false;
	Update([&](Accounts &accounts) {
		oauth::Token &stored = accounts.Get(service).token;
		if (SameToken(stored, stale)) {
			stored = next;
			replaced = true;
		}
	});
	return replaced;
}

std::optional<oauth::Token> AccountStore::Refresh(Service service, const oauth::Token &stale,
						  const std::atomic<bool> *cancel)
{
	// Held across the request: a Twitch refresh token works once, so a second plugin must wait and then take
	// the token this one saved instead of spending the same refresh token.
	NamedLock refreshing(kRefreshLock, kRefreshLockMs);
	if (!refreshing.Held())
		return std::nullopt;

	const Account stored = Load().Get(service);
	switch (NextRefreshStep(stored.token, stale)) {
	case RefreshStep::UseStored:
		return stored.token;
	case RefreshStep::SignedOut:
		return std::nullopt;
	case RefreshStep::Refresh:
		break;
	}

	const oauth::Provider provider = service == Service::Twitch
						 ? oauth::TwitchProvider(stored.clientId)
						 : oauth::GoogleProvider(stored.clientId, stored.clientSecret);
	if (provider.clientId.empty())
		return std::nullopt;
	CurlHttpClient http(cancel);
	auto res = http.Post(provider.tokenUrl, {}, oauth::BuildRefreshBody(provider, stored.token.refreshToken),
			     "application/x-www-form-urlencoded");
	auto parsed =
		oauth::ParseTokenResponse(res.status, res.body, (int64_t)std::time(nullptr), stored.token.refreshToken);
	if (parsed.status != oauth::PollStatus::Granted)
		return std::nullopt;
	if (!ReplaceIfCurrent(service, stored.token, parsed.token))
		return Load().Get(service).token; // the user signed in again meanwhile: that sign-in wins
	return parsed.token;
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

} // namespace unified_chat
