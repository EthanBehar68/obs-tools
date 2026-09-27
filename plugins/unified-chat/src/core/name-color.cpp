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

#include "name-color.hpp"
#include "core/text-util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

namespace unified_chat {

namespace {

constexpr int kSearchSteps = 20;

struct Rgb {
	double r, g, b; // 0..1
};

struct Hsl {
	double h, s, l; // h in 0..360, s and l in 0..1
};

std::optional<Rgb> ParseRgb(std::string_view text)
{
	std::string hex = SanitizeColor(text);
	if (hex.empty())
		return std::nullopt;
	unsigned long value = std::stoul(hex.substr(1), nullptr, 16);
	return Rgb{((value >> 16) & 0xFF) / 255.0, ((value >> 8) & 0xFF) / 255.0, (value & 0xFF) / 255.0};
}

// Rounds to the 8-bit value the color will actually be shown with.
Rgb Quantize(Rgb c)
{
	auto channel = [](double v) {
		return std::lround(std::clamp(v, 0.0, 1.0) * 255.0) / 255.0;
	};
	return {channel(c.r), channel(c.g), channel(c.b)};
}

std::string ToHex(Rgb c)
{
	char buffer[8];
	std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", (int)std::lround(c.r * 255.0),
		      (int)std::lround(c.g * 255.0), (int)std::lround(c.b * 255.0));
	return buffer;
}

// WCAG 2 relative luminance.
double Luminance(Rgb c)
{
	auto linear = [](double v) {
		return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
	};
	return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

double Contrast(double luminanceA, double luminanceB)
{
	return (std::max(luminanceA, luminanceB) + 0.05) / (std::min(luminanceA, luminanceB) + 0.05);
}

Hsl ToHsl(Rgb c)
{
	double max = std::max({c.r, c.g, c.b});
	double min = std::min({c.r, c.g, c.b});
	double l = (max + min) / 2.0;
	double d = max - min;
	if (d == 0.0)
		return {0.0, 0.0, l};
	double s = d / (1.0 - std::fabs(2.0 * l - 1.0));
	double h;
	if (max == c.r)
		h = std::fmod((c.g - c.b) / d, 6.0);
	else if (max == c.g)
		h = (c.b - c.r) / d + 2.0;
	else
		h = (c.r - c.g) / d + 4.0;
	h *= 60.0;
	if (h < 0.0)
		h += 360.0;
	return {h, s, l};
}

Rgb ToRgb(Hsl c)
{
	double chroma = (1.0 - std::fabs(2.0 * c.l - 1.0)) * c.s;
	double x = chroma * (1.0 - std::fabs(std::fmod(c.h / 60.0, 2.0) - 1.0));
	double m = c.l - chroma / 2.0;
	Rgb out;
	if (c.h < 60.0)
		out = {chroma, x, 0.0};
	else if (c.h < 120.0)
		out = {x, chroma, 0.0};
	else if (c.h < 180.0)
		out = {0.0, chroma, x};
	else if (c.h < 240.0)
		out = {0.0, x, chroma};
	else if (c.h < 300.0)
		out = {x, 0.0, chroma};
	else
		out = {chroma, 0.0, x};
	return {out.r + m, out.g + m, out.b + m};
}

// Smallest lightness change that makes fg reach kMinNameContrast on bg, moving towards white or black,
// whichever contrasts more with bg. Ends at that extreme when the threshold can't be reached.
Rgb AdjustForContrast(Rgb fg, double bgLuminance)
{
	if (Contrast(Luminance(fg), bgLuminance) >= kMinNameContrast)
		return fg;

	const Hsl hsl = ToHsl(fg);
	const bool lighten = Contrast(1.0, bgLuminance) >= Contrast(0.0, bgLuminance);
	double pass = lighten ? 1.0 : 0.0; // closest known lightness that reaches the threshold (or the extreme)
	double fail = hsl.l;               // farthest known lightness that doesn't
	for (int i = 0; i < kSearchSteps; ++i) {
		double mid = (pass + fail) / 2.0;
		if (Contrast(Luminance(Quantize(ToRgb({hsl.h, hsl.s, mid}))), bgLuminance) >= kMinNameContrast)
			pass = mid;
		else
			fail = mid;
	}
	return Quantize(ToRgb({hsl.h, hsl.s, pass}));
}

} // namespace

double ContrastRatio(std::string_view a, std::string_view b)
{
	auto ca = ParseRgb(a);
	auto cb = ParseRgb(b);
	if (!ca || !cb)
		return 0.0;
	return Contrast(Luminance(*ca), Luminance(*cb));
}

void NameColorResolver::SetBackground(std::string_view background)
{
	std::string normalized = SanitizeColor(background);
	if (normalized == background_)
		return;
	background_ = std::move(normalized);
	cache_.clear();
	auto bg = ParseRgb(background_);
	hasBackground_ = bg.has_value();
	backgroundLuminance_ = bg ? Luminance(*bg) : 0.0;
}

std::string NameColorResolver::Resolve(const std::string &color)
{
	if (!hasBackground_)
		return color;
	if (auto it = cache_.find(color); it != cache_.end())
		return it->second;

	auto fg = ParseRgb(color);
	if (!fg)
		return color;
	if (cache_.size() >= kMaxEntries)
		cache_.clear();
	std::string readable = ToHex(AdjustForContrast(*fg, backgroundLuminance_));
	cache_.emplace(color, readable);
	return readable;
}

} // namespace unified_chat
