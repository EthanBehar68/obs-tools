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

#pragma once

#include <obs.h>

#include <optional>
#include <string>

namespace stream_alerts {

// The item selected in OBS's Sources list. OBS has no API for adding to that list's right-click menu, so alerts
// take the selection from a button or the Tools menu instead.
struct SelectedSource {
	std::string name;
	bool isText = false;
	// A group or nested scene holding exactly one Text source: that source's name, for the alert's text.
	std::string textInside;
};

// The scene the Sources list shows (the preview scene in Studio Mode). With several selected, the first one OBS
// lists from the top, groups' contents included. UI thread.
std::optional<SelectedSource> GetSelectedSource();

bool IsTextSource(obs_source_t *source);

} // namespace stream_alerts
