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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace stream_alerts {

enum class AlertKind {
	Follow,     // Twitch
	Subscriber, // YouTube channel subscriber
	TwitchSub,  // a new sub, or a resub shared in chat (variant)
	GiftSubs,   // one gifted sub, or a gift bomb ("gifted 5 subs")
	Cheer,
	Raid,
	SuperChat, // Super Chats and Super Stickers
	Member,    // a new YouTube member, or a milestone (variant)
	GiftMemberships,
};
constexpr AlertKind kAllKinds[] = {AlertKind::Follow,    AlertKind::TwitchSub, AlertKind::GiftSubs,
				   AlertKind::Cheer,     AlertKind::Raid,      AlertKind::Subscriber,
				   AlertKind::SuperChat, AlertKind::Member,    AlertKind::GiftMemberships};

inline bool IsTwitch(AlertKind kind)
{
	return kind == AlertKind::Follow || kind == AlertKind::TwitchSub || kind == AlertKind::GiftSubs ||
	       kind == AlertKind::Cheer || kind == AlertKind::Raid;
}
// Paid alerts and raids: each plays on its own, every time, ahead of waiting follows and subscribers.
inline bool IsPriority(AlertKind kind)
{
	return kind != AlertKind::Follow && kind != AlertKind::Subscriber;
}

struct AlertEvent {
	AlertKind kind = AlertKind::Follow;
	std::string id;   // the platform's user / channel id: one follow / subscriber alert per person per stream
	std::string name; // display name
	// Details for the message placeholders; new fields go at the end (positional initialization).
	bool variant = false; // TwitchSub: a resub; Member: a milestone
	int64_t months = 0;   // {months}: resub / milestone
	int64_t count = 0;    // {count}: gifts; {amount} for a cheer's bits
	int64_t viewers = 0;  // {viewers}: raid
	std::string amount;   // {amount}: Super Chat "$5.00"; a cheer's bits
	std::string message;  // {message}: the viewer's comment
	std::string level;    // {level}: membership level
};

struct Alert {
	AlertKind kind = AlertKind::Follow;
	std::vector<std::string> names; // one, or everyone a combined alert covers
	bool combined = false;
	AlertEvent event; // a single alert's details
};

// Alerts wait here and play one at a time. Up to kMaxQueued individual follow / subscriber alerts wait; past that,
// a kind's further events join one combined alert at the end ("+14 more new followers!"), so a follow-bot flood
// doesn't take over the screen for half an hour, and nobody is dropped. Priority alerts (IsPriority) are never
// combined or limited, repeat for the same person, and go ahead of waiting follows and subscribers.
class AlertQueue {
public:
	static constexpr size_t kMaxQueued = 10;

	// False when this person already alerted this stream (an unfollow and refollow, a repeated poll).
	bool Push(const AlertEvent &event);
	std::optional<Alert> Pop();
	bool Empty() const { return queue_.empty(); }
	size_t Size() const { return queue_.size(); }
	// A new stream: waiting alerts are dropped and everyone may alert again.
	void Reset();

private:
	std::deque<Alert> queue_;
	size_t individual_ = 0;
	std::unordered_set<std::string> seen_;
};

// single: "{name} just followed!", with {months} {count} {amount} {viewers} {message} {level} from the event too;
// combined: "+{count} more new followers!", followed on a second line by as many names as fit in maxNamesLength
// characters.
std::string FormatAlert(const Alert &alert, const std::string &single, const std::string &combined,
			size_t maxNamesLength = 80);

} // namespace stream_alerts
