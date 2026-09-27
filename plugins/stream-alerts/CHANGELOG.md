# Changelog

## 0.1.0 (unreleased)

- First version: Twitch follower alerts (EventSub `channel.follow` v2 over a WebSocket) and YouTube subscriber alerts (`myRecentSubscribers`, every 2 minutes), only while OBS is streaming.
- An alert writes its message into a text source and shows a source (e.g. a group with a media source) for 5 seconds, adjustable. One at a time with a 1-second gap; past 10 waiting, the rest combine into one "+N more" alert; once per person per stream.
- **Test alert** buttons, and status lines per platform.
- Each new follower and subscriber (and test alert) is announced to other OBS Tools plugins, so Unified Chat shows it in the dock.
- Message boxes take several lines: Enter starts a new line of the alert text.
- Uses the shared OBS Tools sign-ins (Tools → OBS Tools → Accounts…).
