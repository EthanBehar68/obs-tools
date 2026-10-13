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

#include "template-files.hpp"

#include <algorithm>
#include <cctype>

namespace stream_alerts {

bool IsImageFile(const std::string &path)
{
	const size_t dot = path.rfind('.');
	const size_t slash = path.find_last_of("/\\");
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return false;
	std::string ext = path.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	for (const char *image : {"png", "jpg", "jpeg", "gif", "webp", "bmp", "tga", "psd"}) {
		if (ext == image)
			return true;
	}
	return false;
}

std::string UniqueName(const std::string &base, const std::function<bool(const std::string &)> &taken)
{
	std::string name = base;
	for (int n = 2; taken(name); ++n)
		name = base + " " + std::to_string(n);
	return name;
}

} // namespace stream_alerts
