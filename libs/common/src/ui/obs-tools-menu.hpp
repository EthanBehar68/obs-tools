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

#include <functional>
#include <string>

class QMenu;
class QString;

namespace unified_chat {

// Tools > OBS Tools, shared by every obs-tools plugin. The first plugin OBS loads creates the submenu with its
// Accounts... entry and publishes it through the libobs proc handler; later plugins find it there. Call from
// obs_module_load (the UI thread).
QMenu *ObsToolsMenu();
void AddObsToolsMenuEntry(const QString &text, std::function<void()> onClick);

// Runs on the UI thread after the Accounts window changed a sign-in or client ID. Token refreshes don't count:
// connections pick those up from the store when they need a token.
void SetAccountsChangedHandler(std::function<void(bool twitch, bool google)> handler);
void ClearAccountsChangedHandler();
void NotifyAccountsChanged(bool twitch, bool google);

// Stream Alerts announces each new follower (Twitch) or subscriber (YouTube), test alerts included, so the chat dock
// can show them. Emitted and handled on the UI thread.
void SetAlertHandler(std::function<void(bool follow, const std::string &name, bool test)> handler);
void ClearAlertHandler();
void NotifyAlert(bool follow, const std::string &name, bool test);

} // namespace unified_chat
