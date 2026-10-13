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

#include "monitoring-device.hpp"

namespace unified_chat {

MonitoringRepair CheckMonitoringDevice(const AudioDevice &current, const std::vector<AudioDevice> &present)
{
	if (current.id.empty() || current.id == "default")
		return {};
	const AudioDevice *sameName = nullptr;
	int named = 0;
	for (const auto &device : present) {
		if (device.id == current.id)
			return {};
		if (!current.name.empty() && device.name == current.name) {
			sameName = &device;
			++named;
		}
	}
	if (named == 0)
		return {MonitoringCheck::Missing, {}};
	if (named > 1)
		return {MonitoringCheck::Ambiguous, {}};
	return {MonitoringCheck::Reselect, *sameName};
}

} // namespace unified_chat
