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

#include "text-util.hpp"

#include <cctype>

namespace unified_chat {

static bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string Trim(std::string_view text)
{
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && IsSpace(text[begin]))
		++begin;
	while (end > begin && IsSpace(text[end - 1]))
		--end;
	return std::string(text.substr(begin, end - begin));
}

std::string ToLower(std::string_view text)
{
	std::string out(text);
	for (auto &c : out)
		c = (char)std::tolower((unsigned char)c);
	return out;
}

size_t Utf8Length(std::string_view text)
{
	size_t count = 0;
	for (unsigned char c : text) {
		if ((c & 0xC0) != 0x80)
			++count;
	}
	return count;
}

std::string StripLineBreaks(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (char c : text) {
		if (c == '\r' || c == '\n')
			out.push_back(' ');
		else if (c != '\0')
			out.push_back(c);
	}
	return out;
}

std::string HtmlEscape(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (char c : text) {
		switch (c) {
		case '&':
			out += "&amp;";
			break;
		case '<':
			out += "&lt;";
			break;
		case '>':
			out += "&gt;";
			break;
		case '"':
			out += "&quot;";
			break;
		case '\'':
			out += "&#39;";
			break;
		default:
			out.push_back(c);
		}
	}
	return out;
}

std::string SanitizeColor(std::string_view color)
{
	std::string value = Trim(color);
	if (value.size() != 7 || value[0] != '#')
		return {};
	for (size_t i = 1; i < value.size(); ++i) {
		if (!std::isxdigit((unsigned char)value[i]))
			return {};
	}
	return ToLower(value);
}

std::string FormatDuration(int64_t seconds)
{
	auto unit = [](int64_t count, const char *name) {
		return std::to_string(count) + " " + name + (count == 1 ? "" : "s");
	};
	if (seconds < 60)
		return unit(seconds < 0 ? 0 : seconds, "second");
	if (seconds < 3600)
		return unit((seconds + 30) / 60, "minute");
	if (seconds < 86400) {
		const int64_t minutes = (seconds % 3600 + 30) / 60;
		std::string text = unit(seconds / 3600, "hour");
		return minutes == 0 || minutes == 60 ? text : text + " " + unit(minutes, "minute");
	}
	return unit((seconds + 43200) / 86400, "day");
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil).
static int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

int64_t ParseRfc3339(std::string_view text)
{
	auto number = [&](size_t pos, size_t len, int &out) {
		if (pos + len > text.size())
			return false;
		out = 0;
		for (size_t i = pos; i < pos + len; ++i) {
			if (!std::isdigit((unsigned char)text[i]))
				return false;
			out = out * 10 + (text[i] - '0');
		}
		return true;
	};

	// YYYY-MM-DDTHH:MM:SS
	int year, month, day, hour, minute, second;
	if (text.size() < 20 || !number(0, 4, year) || text[4] != '-' || !number(5, 2, month) || text[7] != '-' ||
	    !number(8, 2, day) || (text[10] != 'T' && text[10] != 't' && text[10] != ' ') || !number(11, 2, hour) ||
	    text[13] != ':' || !number(14, 2, minute) || text[16] != ':' || !number(17, 2, second))
		return 0;
	if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
		return 0;

	size_t pos = 19;
	if (pos < text.size() && text[pos] == '.') {
		++pos;
		while (pos < text.size() && std::isdigit((unsigned char)text[pos]))
			++pos;
	}

	int64_t offset = 0;
	if (pos < text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {
		offset = 0;
	} else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
		int oh, om;
		if (!number(pos + 1, 2, oh) || pos + 3 >= text.size() || text[pos + 3] != ':' ||
		    !number(pos + 4, 2, om))
			return 0;
		offset = (text[pos] == '+' ? 1 : -1) * (int64_t)(oh * 3600 + om * 60);
	} else {
		return 0;
	}

	return DaysFromCivil(year, (unsigned)month, (unsigned)day) * 86400 + hour * 3600 + minute * 60 + second -
	       offset;
}

std::string UrlEncode(std::string_view text)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(text.size() * 3);
	for (unsigned char c : text) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out.push_back((char)c);
		} else {
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 0x0F]);
		}
	}
	return out;
}

std::string FormEncode(const std::vector<std::pair<std::string, std::string>> &fields)
{
	std::string out;
	for (const auto &[key, value] : fields) {
		if (!out.empty())
			out.push_back('&');
		out += UrlEncode(key);
		out.push_back('=');
		out += UrlEncode(value);
	}
	return out;
}

} // namespace unified_chat
