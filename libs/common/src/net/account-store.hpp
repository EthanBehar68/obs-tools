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

#include "core/accounts.hpp"

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace obs_tools {

// The shared accounts file. Every plugin has its own instance; system-wide named locks keep their reads,
// writes and token refreshes from interleaving. Safe to use from any thread. The file is small and only read on
// sign-in, connect and refresh, so it isn't cached.
class AccountStore {
public:
	explicit AccountStore(std::string dir);

	// cancel (on worker threads): stop waiting for the lock once it's set, so closing OBS never waits behind
	// another plugin. The read or write still happens, unlocked, as after a lock timeout.
	Accounts Load(SecretReport *report = nullptr, const std::atomic<bool> *cancel = nullptr) const;
	// Read, change and write under the lock, so another plugin's write in between isn't lost.
	void Update(const std::function<void(Accounts &)> &change, const std::atomic<bool> *cancel = nullptr);

	struct RefreshResult {
		oauth::RefreshOutcome outcome = oauth::RefreshOutcome::Transient;
		oauth::Token token; // when Granted
		bool Granted() const { return outcome == oauth::RefreshOutcome::Granted; }
		bool Rejected() const { return outcome == oauth::RefreshOutcome::Rejected; }
	};
	// For a token the caller found expired or rejected: the token to use instead, either one another plugin
	// already refreshed or a fresh refresh (saved before this returns). Rejected: the sign-in is gone (refused,
	// or nothing to refresh with). Transient: the refresh couldn't be done now (network, server, lock); keep the
	// sign-in. Blocks for the refresh request; call it from a worker thread.
	RefreshResult Refresh(Service service, const oauth::Token &stale, const std::atomic<bool> *cancel = nullptr);
	// stale was rejected and Refresh said Rejected: sign out, unless the stored token has changed since.
	void Invalidate(Service service, const oauth::Token &stale);
	// A refresh done elsewhere (the YouTube chat session refreshes by itself): saved if stale is still stored.
	void Replace(Service service, const oauth::Token &stale, const oauth::Token &next);

private:
	bool ReplaceIfCurrent(Service service, const oauth::Token &stale, const oauth::Token &next,
			      const std::atomic<bool> *cancel = nullptr);

	std::string dir_;
	std::string path_;
};

// plugin_config/obs-tools/accounts.json, next to the calling plugin's own config folder.
AccountStore &SharedAccounts();

} // namespace obs_tools
