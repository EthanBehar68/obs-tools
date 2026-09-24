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

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace unified_chat {

// WCAG 2 contrast ratio a name color must reach against the chat background. 3.0 is WCAG's level for bold
// text; names are bold. On Yami's chat background (#3c404d) it leaves the brand defaults untouched.
constexpr double kMinNameContrast = 3.0;

// WCAG 2 contrast ratio between two "#rrggbb" colors, or 0 when either is not one.
double ContrastRatio(std::string_view a, std::string_view b);

// Makes name colors readable on the chat background by changing only their lightness (hue kept).
// Each color is computed once per background and then served from a cache.
class NameColorResolver {
public:
	static constexpr size_t kMaxEntries = 512;

	// Clears the cache when the background changes. An invalid background disables adjustment.
	void SetBackground(std::string_view background);
	const std::string &Background() const { return background_; }

	// color must be "#rrggbb"; anything else is returned unchanged.
	std::string Resolve(const std::string &color);
	size_t CacheSize() const { return cache_.size(); }

private:
	std::string background_;
	double backgroundLuminance_ = 0.0;
	bool hasBackground_ = false;
	std::unordered_map<std::string, std::string> cache_;
};

} // namespace unified_chat
