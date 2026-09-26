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

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace unified_chat {

std::string Trim(std::string_view text);
std::string ToLower(std::string_view text);

// Number of Unicode code points in a UTF-8 string (invalid bytes count as one each).
size_t Utf8Length(std::string_view text);

// Removes CR, LF and NUL so user input can never inject extra protocol lines.
std::string StripLineBreaks(std::string_view text);

std::string HtmlEscape(std::string_view text);

// Returns a normalized "#rrggbb" color, or an empty string when the input is not a hex color.
std::string SanitizeColor(std::string_view color);

// "2026-09-26T02:13:45.123+00:00" or "...Z" (RFC 3339) -> unix seconds; 0 when it can't be parsed.
int64_t ParseRfc3339(std::string_view text);

std::string UrlEncode(std::string_view text);
std::string FormEncode(const std::vector<std::pair<std::string, std::string>> &fields);

} // namespace unified_chat
