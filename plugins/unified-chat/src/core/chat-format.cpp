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
#include "emotes.hpp"
#include "mentions.hpp"
#include "name-color.hpp"
#include "core/text-util.hpp"

#include <algorithm>

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
			      NameColorResolver *colors, const EmoteIndex *emotes, int emoteHeight,
			      int *renderedTextLength)
{
	std::string color = SanitizeColor(message.color);
	if (color.empty())
		color = std::string(DefaultNameColor(message.platform));
	if (colors)
		color = colors->Resolve(color);

	const std::string imageSize = "\" width=\"" + std::to_string(iconSize) + "\" height=\"" +
				      std::to_string(iconSize) + "\" style=\"vertical-align: middle;\"> ";
	std::string html;
	for (Platform platform : platforms)
		html += "<img src=\"" + std::string(IconResource(platform)) + imageSize;
	if (message.isSelf)
		html += "<img src=\"" + std::string(kSelfBadgeResource) + imageSize;
	for (const auto &badge : message.badges)
		html += "<img src=\"" + HtmlEscape(BadgeImageKey(badge)) + "\" title=\"" + HtmlEscape(badge.set) +
			imageSize;
	const std::string name =
		"<span style=\"color: " + color + "; font-weight: bold;\">" + HtmlEscape(message.author) + "</span>";
	// Other people's names are links: clicking one starts a mention (and on Twitch, a reply). The inner span
	// keeps the name color instead of the palette's link color.
	if (!message.isSelf && !message.mention.empty())
		html += "<a href=\"" + HtmlEscape(BuildMentionLink(message.platform, message.mention, message.id)) +
			"\" style=\"text-decoration: none;\">" + name + "</a>";
	else
		html += name;

	if (!message.replyTo.empty())
		html += " <span style=\"color: #9a9a9a;\">\xE2\x86\x92 @" + HtmlEscape(message.replyTo) + "</span>";

	// The message text always comes last and keeps its spaces (pre-wrap), so the view can find it by length to
	// strike it through when a moderator removes it. Emotes are images of a known shape, so the line is laid out
	// at its final size before the image arrives.
	const int height = emoteHeight > 0 ? emoteHeight : iconSize;
	const bool twitch = message.platform == Platform::Twitch;
	int length = 0;
	std::string text;
	for (const auto &segment : SplitMessage(message.text, message.emotes, twitch ? emotes : nullptr)) {
		if (!segment.emote) {
			text += HtmlEscape(segment.text);
			length += (int)Utf16Length(segment.text);
			continue;
		}
		const Emote &emote = *segment.emote;
		const int width = std::max(1, (int)(height * emote.aspect + 0.5));
		text += "<img src=\"" + HtmlEscape(EmoteImageKey(emote)) + "\" title=\"" + HtmlEscape(emote.name) +
			"\" width=\"" + std::to_string(width) + "\" height=\"" + std::to_string(height) +
			"\" style=\"vertical-align: middle;\">";
		length += 1; // an image is one character in the document
	}
	if (renderedTextLength)
		*renderedTextLength = length;

	if (message.isAction)
		html += " <span style=\"color: " + color + "; font-style: italic; white-space: pre-wrap;\">" + text +
			"</span>";
	else
		html += ": <span style=\"white-space: pre-wrap;\">" + text + "</span>";
	return html;
}

ModerationTag TagFor(const ModerationEvent &event)
{
	switch (event.kind) {
	case ModerationEvent::Kind::RemoveUser:
		if (event.durationSeconds > 0)
			return {2, "(timed out " + FormatDuration(event.durationSeconds) + ")", kTagTimeoutColor};
		return {3, "(banned)", kTagBanColor};
	case ModerationEvent::Kind::ClearChat:
		return {1, "(chat cleared)", kTagGreyColor};
	default:
		return {1, "(deleted)", kTagGreyColor};
	}
}

std::string ModerationNotice(const ModerationEvent &event, std::string_view name)
{
	const std::string platform(PlatformName(event.platform));
	switch (event.kind) {
	case ModerationEvent::Kind::RemoveUser:
		if (event.durationSeconds > 0)
			return platform + ": " + std::string(name) + " was timed out for " +
			       FormatDuration(event.durationSeconds);
		return platform + ": " + std::string(name) + " was banned";
	case ModerationEvent::Kind::ClearChat:
		return platform + ": chat was cleared by a moderator";
	default:
		return {};
	}
}

std::string FormatNoticeHtml(std::string_view text)
{
	return "<span style=\"color: #9a9a9a; font-style: italic;\">" + HtmlEscape(text) + "</span>";
}

std::string FormatAlertHtml(Platform platform, std::string_view name, bool test, int iconSize,
			    NameColorResolver *colors)
{
	std::string color(DefaultNameColor(platform));
	if (colors)
		color = colors->Resolve(color);
	const std::string size = std::to_string(iconSize);
	std::string html = "<img src=\"" + std::string(IconResource(platform)) + "\" width=\"" + size + "\" height=\"" +
			   size + "\" style=\"vertical-align: middle;\"> <b style=\"color: " + color + ";\">\u2605 " +
			   HtmlEscape(name) + (platform == Platform::Twitch ? " followed" : " subscribed") + "</b>";
	if (test)
		html += " <i style=\"color: #9a9a9a;\">(test)</i>";
	return html;
}

} // namespace unified_chat
