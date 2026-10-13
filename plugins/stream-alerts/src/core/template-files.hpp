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

#include <functional>
#include <string>

namespace stream_alerts {

// Whether a picture file is a still or animated image (Image Source) rather than a video (Media Source), by its
// extension.
bool IsImageFile(const std::string &path);

// base, or "base 2", "base 3"... the first that taken() says is free, as OBS names a pasted source.
std::string UniqueName(const std::string &base, const std::function<bool(const std::string &)> &taken);

// File dialog filters.
constexpr const char *kPictureFilter = "*.png *.jpg *.jpeg *.gif *.webp *.bmp *.tga *.mp4 *.webm *.mov *.mkv";
constexpr const char *kSoundFilter = "*.mp3 *.wav *.ogg *.opus *.flac *.m4a *.aac";

} // namespace stream_alerts
