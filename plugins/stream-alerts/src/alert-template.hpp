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

#include <optional>
#include <string>

namespace stream_alerts {

// What "Create group…" builds: a hidden group holding a Text (GDI+) source and, optionally, a picture (image, GIF or
// video) and a sound, laid out the way that plays reliably (the group's eye off, its contents' on).
struct AlertTemplate {
	std::string name;       // the group's; the text source is "<name> Text". Made unique if taken.
	std::string sampleText; // shown until the first alert, to position it by
	std::string picture;    // file path, or empty
	std::string sound;      // file path, or empty
	bool everyScene = true; // also add the group to every other scene, as Paste (Reference) does
};

struct CreatedAlert {
	std::string group;
	std::string text;
	int scenes = 0; // the group was placed in this many scenes
};

// In the scene the Sources list shows (the preview scene in Studio Mode). Picture and sound files play with
// "Restart when shown" and, for sound, Monitor and Output. nullopt when there's no scene. UI thread.
std::optional<CreatedAlert> CreateAlertGroup(const AlertTemplate &alert);

} // namespace stream_alerts
