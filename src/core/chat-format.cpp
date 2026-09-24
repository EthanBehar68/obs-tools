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

#include "chat-format.hpp"
#include "name-color.hpp"
#include "text-util.hpp"

namespace unified_chat {

std::string_view DefaultNameColor(Platform platform)
{
	return platform == Platform::Twitch ? "#a970ff" : "#ff4e45";
}

std::string_view IconResource(Platform platform)
{
	return platform == Platform::Twitch ? kTwitchIconResource : kYouTubeIconResource;
}

std::string FormatMessageHtml(const ChatMessage &message, int iconSize)
{
	return FormatMessageHtml(message, iconSize, {message.platform});
}

std::string FormatMessageHtml(const ChatMessage &message, int iconSize, const std::vector<Platform> &platforms,
			      NameColorResolver *colors)
{
	std::string color = SanitizeColor(message.color);
	if (color.empty())
		color = std::string(DefaultNameColor(message.platform));
	if (colors)
		color = colors->Resolve(color);

	std::string size = std::to_string(iconSize);
	std::string html;
	for (Platform platform : platforms)
		html += "<img src=\"" + std::string(IconResource(platform)) + "\" width=\"" + size + "\" height=\"" +
			size + "\" style=\"vertical-align: middle;\"> ";
	html += "<span style=\"color: " + color + "; font-weight: bold;\">" + HtmlEscape(message.author) + "</span>";

	if (message.isAction)
		html += " <span style=\"color: " + color + "; font-style: italic;\">" + HtmlEscape(message.text) +
			"</span>";
	else
		html += ": " + HtmlEscape(message.text);
	return html;
}

std::string FormatNoticeHtml(std::string_view text)
{
	return "<span style=\"color: #9a9a9a; font-style: italic;\">" + HtmlEscape(text) + "</span>";
}

} // namespace unified_chat
