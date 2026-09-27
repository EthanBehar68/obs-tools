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

namespace unified_chat {

// The shared accounts file. Every plugin has its own instance; system-wide named locks keep their reads,
// writes and token refreshes from interleaving. Safe to use from any thread. The file is small and only read on
// sign-in, connect and refresh, so it isn't cached.
class AccountStore {
public:
	explicit AccountStore(std::string dir);

	Accounts Load(SecretReport *report = nullptr) const;
	// Read, change and write under the lock, so another plugin's write in between isn't lost.
	void Update(const std::function<void(Accounts &)> &change);

	// For a token the caller found expired or rejected: the token to use instead, either one another plugin
	// already refreshed or a fresh refresh (saved before this returns). nullopt: signed out, or the refresh
	// failed. Blocks for the refresh request; call it from a worker thread.
	std::optional<oauth::Token> Refresh(Service service, const oauth::Token &stale,
					    const std::atomic<bool> *cancel = nullptr);
	// stale was rejected and couldn't be refreshed: sign out, unless the stored token has changed since.
	void Invalidate(Service service, const oauth::Token &stale);
	// A refresh done elsewhere (the YouTube chat session refreshes by itself): saved if stale is still stored.
	void Replace(Service service, const oauth::Token &stale, const oauth::Token &next);

private:
	bool ReplaceIfCurrent(Service service, const oauth::Token &stale, const oauth::Token &next);

	std::string dir_;
	std::string path_;
};

// plugin_config/obs-tools/accounts.json, next to the calling plugin's own config folder.
AccountStore &SharedAccounts();

} // namespace unified_chat
