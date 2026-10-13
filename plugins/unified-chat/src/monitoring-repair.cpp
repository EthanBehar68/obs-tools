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

#include "monitoring-repair.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/config-file.h>

#include <vector>

namespace unified_chat {

static bool AddDevice(void *data, const char *name, const char *id)
{
	if (name && id)
		static_cast<std::vector<AudioDevice> *>(data)->push_back({name, id});
	return true;
}

MonitoringRepair RepairMonitoringDevice(AudioDevice &current)
{
	current = {};
	if (!obs_audio_monitoring_available())
		return {};
	const char *name = nullptr;
	const char *id = nullptr;
	obs_get_audio_monitoring_device(&name, &id);
	current = {name ? name : "", id ? id : ""}; // copied: selecting another device frees these

	std::vector<AudioDevice> present;
	obs_enum_audio_monitoring_devices(AddDevice, &present);
	MonitoringRepair repair = CheckMonitoringDevice(current, present);
	switch (repair.check) {
	case MonitoringCheck::Fine:
		break;
	case MonitoringCheck::Reselect: {
		const AudioDevice &device = repair.device;
		if (!obs_set_audio_monitoring_device(device.name.c_str(), device.id.c_str())) {
			obs_log(LOG_WARNING, "monitoring device '%s' has a new ID, but selecting it failed",
				device.name.c_str());
			repair.check = MonitoringCheck::Missing;
			break;
		}
		// Where Settings > Audio keeps it, so the next start uses the new ID too.
		if (config_t *config = obs_frontend_get_profile_config()) {
			config_set_string(config, "Audio", "MonitoringDeviceName", device.name.c_str());
			config_set_string(config, "Audio", "MonitoringDeviceId", device.id.c_str());
			config_save_safe(config, "tmp", nullptr);
		}
		obs_log(LOG_INFO, "monitoring device '%s' had a stale ID (%s); selected it again with its new ID (%s)",
			device.name.c_str(), current.id.c_str(), device.id.c_str());
		break;
	}
	case MonitoringCheck::Missing:
		obs_log(LOG_WARNING, "monitoring device '%s' (%s) isn't connected: monitored audio won't play locally",
			current.name.c_str(), current.id.c_str());
		break;
	case MonitoringCheck::Ambiguous:
		obs_log(LOG_WARNING,
			"monitoring device '%s' has a stale ID and several devices have that name: not changed",
			current.name.c_str());
		break;
	}
	return repair;
}

} // namespace unified_chat
