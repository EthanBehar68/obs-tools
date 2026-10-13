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

#include "highlights.hpp"
#include "name-color.hpp"
#include "core/text-util.hpp"

#include <cmath>
#include <cstdio>
#include <optional>

namespace unified_chat {

static constexpr const char *kAlertColor = "#e8a317"; // amber: apart from names, mentions and first-time chatters
static constexpr const char *kFirstChatterColor = "#3fb950";

const char *HighlightKey(HighlightKind kind)
{
	static constexpr const char *kKeys[] = {"follow",     "twitch_sub",    "gift_subs",
						"cheer",      "raid",          "subscriber",
						"super_chat", "member",        "gift_memberships",
						"jewels",     "first_chatter", "mention"};
	static_assert(std::size(kKeys) == kHighlightKinds);
	return kKeys[(size_t)kind];
}

Highlight DefaultHighlight(HighlightKind kind)
{
	switch (kind) {
	case HighlightKind::Follow:
	case HighlightKind::Subscriber:
		return {true, kAlertColor, "♥"}; // ♥
	case HighlightKind::TwitchSub:
		return {true, kAlertColor, "★"}; // ★
	case HighlightKind::GiftSubs:
	case HighlightKind::GiftMemberships:
		return {true, kAlertColor, "\U0001F381"}; // 🎁
	case HighlightKind::Cheer:
		return {true, kAlertColor, "\U0001F48E"}; // 💎
	case HighlightKind::Raid:
		return {true, kAlertColor, "⚔"}; // ⚔
	case HighlightKind::SuperChat:
		return {true, kAlertColor, "\U0001F4B2"}; // 💲
	case HighlightKind::Member:
		return {true, kAlertColor, "\U0001F3C5"}; // 🏅
	case HighlightKind::Jewels:
		return {true, kAlertColor, "\U0001F4A0"}; // 💠 (💎 is the cheer's)
	case HighlightKind::FirstChatter:
		return {true, kFirstChatterColor, "✨"}; // ✨
	default:
		return {true, {}, {}}; // mention: the theme's tint, no icon
	}
}

Highlights::Highlights()
{
	for (size_t i = 0; i < kHighlightKinds; ++i)
		kinds[i] = DefaultHighlight((HighlightKind)i);
}

std::string SanitizeIcon(std::string_view icon)
{
	std::string out = Trim(StripLineBreaks(icon));
	if (out.size() <= kMaxIconBytes)
		return out;
	size_t end = kMaxIconBytes;
	while (end > 0 && ((unsigned char)out[end] & 0xC0) == 0x80)
		--end; // not inside a UTF-8 sequence
	return out.substr(0, end);
}

namespace {

struct Rgb {
	double r, g, b;
};

std::optional<Rgb> Parse(std::string_view hex)
{
	const std::string color = SanitizeColor(hex);
	if (color.empty())
		return std::nullopt;
	unsigned value = 0;
	std::sscanf(color.c_str() + 1, "%6x", &value);
	return Rgb{double((value >> 16) & 0xFF), double((value >> 8) & 0xFF), double(value & 0xFF)};
}

std::string Hex(Rgb c)
{
	char out[8];
	std::snprintf(out, sizeof(out), "#%02x%02x%02x", (int)std::lround(c.r), (int)std::lround(c.g),
		      (int)std::lround(c.b));
	return out;
}

double Luma(Rgb c)
{
	return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0;
}

} // namespace

HighlightStyle ComputeHighlightStyle(std::string_view accent, std::string_view background)
{
	auto fg = Parse(accent);
	auto bg = Parse(background);
	if (!fg || !bg)
		return {};
	// A dark theme takes a little more of the accent to show; a light one less, so text stays crisp.
	const double amount = Luma(*bg) < 0.5 ? 0.30 : 0.22;
	const Rgb tint{bg->r + (fg->r - bg->r) * amount, bg->g + (fg->g - bg->g) * amount,
		       bg->b + (fg->b - bg->b) * amount};
	HighlightStyle style;
	style.background = Hex(tint);
	NameColorResolver readable;
	readable.SetBackground(style.background);
	style.text = readable.Resolve(SanitizeColor(accent));
	return style;
}

std::optional<ChatEvent> EventFromMessage(const ChatMessage &message)
{
	ChatEvent event;
	event.platform = message.platform;
	if (message.bits > 0) {
		event.kind = HighlightKind::Cheer;
		event.text = message.author + " cheered " + std::to_string(message.bits) + " bits";
		event.comment = message.text;
		return event;
	}
	if (!message.youtubeEvent)
		return std::nullopt;
	const YouTubeEvent &yt = *message.youtubeEvent;
	const std::string level = yt.level.empty() ? std::string() : " (" + yt.level + ")";
	switch (yt.kind) {
	case YouTubeEvent::Kind::SuperChat:
		event.kind = HighlightKind::SuperChat;
		event.text = message.author + " sent " + yt.amount;
		event.comment = yt.message;
		break;
	case YouTubeEvent::Kind::SuperSticker:
		event.kind = HighlightKind::SuperChat;
		event.text = message.author + " sent a Super Sticker (" + yt.amount + ")";
		event.comment = yt.message; // the sticker's description
		break;
	case YouTubeEvent::Kind::NewMember:
		event.kind = HighlightKind::Member;
		event.text = message.author + " became a member" + level;
		break;
	case YouTubeEvent::Kind::MemberMilestone:
		event.kind = HighlightKind::Member;
		event.text = message.author + " has been a member for " + std::to_string(yt.months) + " months" + level;
		event.comment = yt.message;
		break;
	case YouTubeEvent::Kind::Jewels: {
		event.kind = HighlightKind::Jewels;
		const std::string gift = yt.gift.empty() ? std::string("a gift") : yt.gift;
		event.text = message.author + " sent " + (yt.count > 1 ? std::to_string(yt.count) + " × " : "") + gift;
		if (const int64_t jewels = JewelsTotal(yt); jewels > 0)
			event.text += " (" + std::to_string(jewels) + " Jewels)";
		if (yt.combo > yt.count)
			event.text += ", " + std::to_string(yt.combo) + " in a row";
		break;
	}
	default:
		event.kind = HighlightKind::GiftMemberships;
		event.text = message.author + " gifted " + std::to_string(yt.count) + " memberships" + level;
	}
	return event;
}

} // namespace unified_chat
