# Plan: Stream info, and assigning alert sources (2026-10-13)

## 1. Stream info (Unified Chat)

Set the title, Twitch category and tags, and YouTube category from OBS.

**Opened from:** a ✎ button on the dock, and **Tools → OBS Tools → Stream Info…**

**Window**
- Title, with Twitch / YouTube checkboxes and a character count (Twitch 140, YouTube 100).
- Twitch category: search as you type, with box art; recently used ones saved in `config.json`.
- Twitch tags: up to 10 chips, each up to 25 letters or digits. They're remembered per category, so picking a game brings its tags back.
- YouTube category: a dropdown (Gaming, Entertainment, …). YouTube's API has no field for the specific game.
- Opens filled with the current values. **Apply** reports each platform's result.

**Twitch**
- New scope `channel:manage:broadcast` in `oauth::TwitchProvider`, so one re-sign-in. If it's missing, the window shows **Sign in again**.
- `GET /helix/channels` (current values), `GET /helix/search/categories` (results cached, requests held back while typing), `PATCH /helix/channels` with `title`, `game_id`, `tags`.
- Only enabled when the dock's Twitch channel is the signed-in user's own.

**YouTube**
- Looks for an **upcoming** or active broadcast (chat looks only for active ones), so the title can be set before going live.
- `videos.list part=snippet` (1 unit), then `videos.update` with the whole snippet (50 units): any field left out is cleared. The window shows the cost.
- No new scope.

**Code**
- `core/stream-info.*`: request builders and parsers, with doctest coverage (Twitch and Google examples).
- `TwitchConnection::UpdateStreamInfo` and `YouTubeConnection::UpdateStreamInfo`: queued on the existing worker threads, as moderation is; results go back to the UI thread.
- `stream-info-dialog.*`: Qt window; box art through `asset-loader`.
- Docs: CHANGELOG (with the re-sign-in note), README, the tests table in BUILDING.md.

## 2. Assigning alert sources (Stream Alerts)

OBS has no plugin API for the Sources right-click menu, so instead:
- **Use selected** buttons next to each kind's source and text source pickers. They take the item selected in the current scene's Sources list.
- **Tools → OBS Tools → Assign selected source to alert ▸ <kind>**

## Later

- Set the Twitch category on scene switch: one per scene, or guessed from a game capture's `.exe`.
- Stream Alerts **Create from template**: builds a group (text source plus an image) in the current scene and assigns it to an alert.

## Still unverified (keep open)

Built 2026-10-13 (sections 1 and 2 above), not yet tried in OBS:

6. Stream Info: sign in to Twitch again, then title / category search and box art / tags / recent categories, Apply on each platform, YouTube before going live (upcoming broadcast), Enter in the search box doesn't Apply.
7. Stream Alerts: Use selected (group with one text source fills both; Studio Mode uses the preview scene), the Tools menu entry, the settings window staying open while you click in Sources.

8. Monitoring repair: change one character of `MonitoringDeviceId` in the profile's `basic.ini` (OBS closed), start OBS: a dock notice, and alerts play in the headphones again.

From 2026-10-12:

1. Twitch re-sign-in, then EventSub chat: sending, your own echo, `/me`, refused messages, role badges. (Verified 2026-10-13: the Watching without audio badge shows.)
2. Highlights: alert lines, Settings → Highlights, first-time chatters ✨.
3. Stream Alerts: every new kind's Test button, priority order, minimums, placeholder checks.
4. YouTube: Super Chat and membership lines and alerts; Jewels needs a real gift (check the logged raw figures).
5. Both plugins together: one EventSub session, no doubled events, clean exit during a token refresh.
