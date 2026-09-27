/*
obs-stream-alerts
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

#include "alerts-controller.hpp"
#include "ui/obs-tools-menu.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-stream-alerts", "en-US")
OBS_MODULE_AUTHOR("ebehar")

using stream_alerts::AlertsController;

static QPointer<AlertsController> s_controller;

static void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	if (!s_controller)
		return;
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		s_controller->Start();
	else if (event == OBS_FRONTEND_EVENT_EXIT)
		s_controller->Shutdown();
	else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED)
		s_controller->OnStreamingChanged(true);
	else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED)
		s_controller->OnStreamingChanged(false);
}

bool obs_module_load(void)
{
	auto mainwin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (mainwin == nullptr)
		return false;

	s_controller = new AlertsController(mainwin); // deleted with the main window
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
	unified_chat::AddObsToolsMenuEntry(QString::fromUtf8(obs_module_text("Tools.StreamAlerts")), [mainwin]() {
		if (s_controller)
			s_controller->OpenSettings(mainwin);
	});
	unified_chat::SetAccountsChangedHandler([](bool twitch, bool google) {
		if (s_controller)
			s_controller->OnAccountsChanged(twitch, google);
	});
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	unified_chat::ClearAccountsChangedHandler();
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	obs_log(LOG_INFO, "plugin unloaded");
}

const char *obs_module_description(void)
{
	return "Twitch follower and YouTube subscriber alerts";
}
