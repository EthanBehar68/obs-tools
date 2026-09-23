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
