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

#pragma once

#include <string>
#include <vector>

namespace unified_chat {

struct AudioDevice {
	std::string name;
	std::string id;
};

// OBS keeps the monitoring device by its Windows ID. When Windows gives the same headset a new ID (another USB port,
// a driver update), OBS keeps asking for the old one, fails ("Failed to get device: 80070490") and monitored sound,
// such as alerts, plays to the stream but not to you.
enum class MonitoringCheck {
	Fine,      // "default", no device set, or the ID is present
	Reselect,  // the ID is gone but exactly one present device has its name: use that one
	Missing,   // no present device has its name (unplugged or renamed)
	Ambiguous, // several present devices have its name: no guessing
};

struct MonitoringRepair {
	MonitoringCheck check = MonitoringCheck::Fine;
	AudioDevice device; // Reselect: the device to use
};

MonitoringRepair CheckMonitoringDevice(const AudioDevice &current, const std::vector<AudioDevice> &present);

} // namespace unified_chat
