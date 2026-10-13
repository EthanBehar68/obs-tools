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

#include "core/monitoring-device.hpp"

namespace unified_chat {

// Checks OBS's monitoring device against the devices Windows has now. When only its ID changed, selects it again
// (monitoring restarts at once) and saves it to the profile, as Settings > Audio would. Logs what it finds.
// current: the device OBS had. UI thread; a few milliseconds.
MonitoringRepair RepairMonitoringDevice(AudioDevice &current);

} // namespace unified_chat
