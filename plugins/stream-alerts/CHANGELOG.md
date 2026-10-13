# Changelog

## Unreleased

- **New alert kinds.** Twitch: subs and resubs, gift subs (a gift bomb is one alert), cheers, raids. YouTube: Super Chats and Super Stickers, new members and milestones, gifted memberships (these come from Unified Chat's YouTube chat, at no extra quota).
- Each kind has its own source, text source and message, with placeholders such as `{months}`, `{count}`, `{amount}`, `{viewers}`, `{message}` and `{level}`; subs and members have a second message for resubs and milestones.
- Paid alerts and raids always play, each on its own and every time, ahead of waiting follows and subscribers. Cheers and raids have a minimum (bits / viewers), 1 by default.
- A kind with no source set is skipped.
- **YouTube subscribers: Check every** 30 seconds to 15 minutes (2 minutes by default); the window shows the quota it costs.
- The settings window lists the kinds on the left, with the selected one's settings and Test buttons beside it.
- Sign in to Twitch again once: the sign-in now also asks for chat (`user:read:chat`) and bits (`bits:read`) access.
- **YouTube Jewels gifts** (needs the matching Unified Chat): one alert per gift, or per batch of a combo, with `{gift}`, `{count}` and `{amount}` (Jewels); a second message for several gifts at once, and a minimum in Jewels.
- Internal: the EventSub WebSocket code is shared with Unified Chat.

## 0.1.0 (2026-09-27)

- First version: Twitch follower alerts (EventSub `channel.follow` v2 over a WebSocket) and YouTube subscriber alerts (`myRecentSubscribers`, every 2 minutes), only while OBS is streaming.
- An alert writes its message into a text source and shows a source (e.g. a group with a media source) for 5 seconds, adjustable. One at a time with a 1-second gap; past 10 waiting, the rest combine into one "+N more" alert; once per person per stream.
- **Test alert** buttons, and status lines per platform.
- Each new follower and subscriber (and test alert) is announced to other OBS Tools plugins, so Unified Chat shows it in the dock.
- Message boxes take several lines: Enter starts a new line of the alert text.
- Uses the shared OBS Tools sign-ins (Tools → OBS Tools → Accounts…).
