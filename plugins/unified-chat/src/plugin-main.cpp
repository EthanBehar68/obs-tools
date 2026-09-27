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

#include "chat-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-unified-chat", "en-US")
OBS_MODULE_AUTHOR("ebehar")

static constexpr const char *kDockId = "obs-unified-chat-dock";

static void OnFrontendEvent(enum obs_frontend_event event, void *private_data)
{
	auto dock = static_cast<unified_chat::ChatDock *>(private_data);
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		dock->Start();
	else if (event == OBS_FRONTEND_EVENT_EXIT)
		dock->Shutdown();
	else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED)
		dock->OnStreamingChanged(true);
	else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED)
		dock->OnStreamingChanged(false);
}

static unified_chat::ChatDock *s_dock = nullptr;

bool obs_module_load(void)
{
	auto mainwin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (mainwin == nullptr)
		return false;

	auto dock = new unified_chat::ChatDock();
	dock->setObjectName(kDockId);
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Title"), dock)) {
		delete dock;
		return false;
	}

	s_dock = dock;
	obs_frontend_add_event_callback(OnFrontendEvent, dock);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	if (s_dock)
		obs_frontend_remove_event_callback(OnFrontendEvent, s_dock);
	s_dock = nullptr;
	obs_log(LOG_INFO, "plugin unloaded");
}

const char *obs_module_description(void)
{
	return "Twitch and YouTube chat in one dock";
}
