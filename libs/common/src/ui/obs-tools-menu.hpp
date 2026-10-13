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
#include <vector>

class QMenu;
class QString;

namespace obs_tools {

// What OBS Tools plugins exchange: the accounts file, the Twitch scopes, the signals below and their JSON. Bump it
// when any of that changes in a way an older plugin would get wrong, and release both plugins together.
constexpr long long kObsToolsProtocol = 2;
// The oldest protocol this plugin still works with. Raise it to kObsToolsProtocol when a bump breaks the older
// one; leave it when the change is only an addition the older plugin can ignore (then no warning is shown).
constexpr long long kObsToolsMinProtocol = 2;

// Call from obs_module_load. Adds this plugin to the shared registry and publishes its version, protocol and Twitch
// scopes (through the libobs proc handler, independent of load order) so the others can find them. name: a proc-safe
// id such as "unified_chat"; module: the DLL name (PLUGIN_NAME).
void RegisterObsToolsPlugin(const char *name, const char *module, const char *version);
// Call at OBS_FRONTEND_EVENT_FINISHED_LOADING: logs the other OBS Tools plugins loaded and their versions, and
// warns when one can't work with this one (its sign-ins, permissions or events may not match).
void CheckObsToolsPlugins();
// Empty, or a sentence for the Accounts window saying which plugin to update.
std::string ObsToolsVersionWarning();
// This plugin's Twitch scopes with every other loaded plugin's: what a sign-in should ask for.
std::string AllTwitchScopes(const std::string &own);

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

// Unified Chat passes on YouTube Super Chats, Super Stickers and memberships from the live chat (core/youtube-events
// JSON), so Stream Alerts can alert on them. Emitted and handled on the UI thread.
void SetYouTubeEventHandler(std::function<void(const std::string &eventJson)> handler);
void ClearYouTubeEventHandler();
void NotifyYouTubeEvent(const std::string &eventJson);

} // namespace obs_tools
