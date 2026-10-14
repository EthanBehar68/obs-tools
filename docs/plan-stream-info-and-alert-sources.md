# Built 2026-10-13: design notes and open tests

Started as the plan for Stream Info and assigning alert sources; now describes what was built that day, and keeps the list of tests still to run in OBS.

## 1. Stream Info (Unified Chat)

Set the title, Twitch category and tags, and YouTube category from OBS. Opened from ✎ on the dock and **Tools → OBS Tools → Stream Info…**; not modal.

**Window**
- One title. The count shows the limit of the platforms ticked under **Change**: 100 with YouTube, otherwise 140.
- Twitch category: search as you type (300 ms after typing pauses; only the latest search runs), with box art. Recent categories (saved in `config.json`, 10) show when the search box is empty and bring back the tags last used with them.
- Twitch tags: one field, separated by commas or spaces, up to 10, each up to 25 letters or digits.
- YouTube category: a fixed dropdown (Gaming, Entertainment, …). YouTube's API has no field for the specific game.
- **Change: Twitch / YouTube** decides which platforms **Apply** updates; only what changed is sent. Each platform's result is shown.

**Twitch**
- Scope `channel:manage:broadcast` in `oauth::TwitchProvider` (one re-sign-in). Without it the window reads but can't change, and shows **Sign in again…**.
- `GET /helix/channels`, `GET /helix/search/categories`, `PATCH /helix/channels` (`title`, `game_id`, `tags`), always on the signed-in account's own channel.

**YouTube**
- The live broadcast, else the soonest upcoming one (chat only looks for live ones), so it can be set before going live.
- Opening or Reload: 2-3 units. Apply: `videos.list part=snippet` again (1), then `videos.update` with the whole writable snippet (50), since a field left out is cleared.

**Code**
- `core/stream-info.*` (tested): request builders, parsers, tag rules, recent categories, scene links.
- `net/stream-info-client.*`: its own thread, only while the window is open (or a category-by-scene request runs). Box art is fetched there and cached per URL in the window.
- `stream-info-dialog.*`: the window.

## 2. Assigning alert sources (Stream Alerts)

OBS has no plugin API for the Sources right-click menu, so instead:
- **Use selected** next to each kind's source and text source pickers: the item selected in the Sources list (the preview scene in Studio Mode). A group or nested scene with exactly one Text source fills in both.
- **Tools → OBS Tools → Assign selected source to alert ▸ <kind>** opens the settings window on that kind, filled in and ticked; OK saves.
- The settings window is no longer modal, so the selection can change while it's open.

## 3. Audio monitoring repair (Unified Chat)

At load, stream start and profile switch: a monitoring device whose Windows ID went stale (`Failed to get device: 80070490`) is selected again by name and saved to the profile's `[Audio]`, with a dock notice. Missing or ambiguous devices get one notice.

## 4. Display options (Unified Chat, Settings → General)

Text size (80-200%), timestamps (off / 24-hour / 12-hour, arrival time, on every line) and compact (emotes 1.2× instead of 1.5×, icon header, 1 px margins). All three change the lines already shown.

## 5. Create group… (Stream Alerts)

A hidden group with a Text (GDI+) source (Arial Bold 64, outlined, the kind's message filled with test details), an optional picture (image, GIF or video) and an optional sound (restart when shown, Monitor and Output). In the scene the Sources list shows, and by default every other scene as a reference.

## 6. Twitch category by scene (Unified Chat, Stream Info)

**Use for scene ▾** links a category to a scene (by uuid). While streaming, once a linked scene has been live for 10 s (0-60), only the category is set, and only if Twitch has another; unlinked scenes leave it. Off by default.

Not planned: guessing the category from a game capture's `.exe` (Twitch has no lookup by executable; name matching is unreliable).

## Still unverified (keep open)

Built 2026-10-13, not yet tried in OBS:

6. Stream Info: sign in to Twitch again, then title / category search and box art / tags / recent categories, Apply on each platform, YouTube before going live (upcoming broadcast), Enter in the search box doesn't Apply.
7. Stream Alerts: Use selected (group with one text source fills both; Studio Mode uses the preview scene), the Tools menu entry, the settings window staying open while you click in Sources.
8. Monitoring repair: change one character of `MonitoringDeviceId` in the profile's `basic.ini` (OBS closed), start OBS: a dock notice, and alerts play in the headphones again.
9. Display (Settings → General): text size up and down with chat on screen (text, icons, badges, emotes resize; moderation strikes still right), timestamps on/off/12h on existing lines and new ones (also after a bot merge and your own Twitch line's update), compact on and off (header icons, dimmed when offline, viewer counts).
10. Stream Alerts Create group…: with a picture and a sound, with neither, and with a video; the group lands hidden in every scene (Studio Mode: built in the preview scene), Test alert plays it with sound you hear, OK keeps it assigned; a second Create gives "… 2".
11. Category by scene: link two scenes (Use for scene ▾), tick the switch, go live: a linked scene sets its category after 10 s (dock notice), a quick hop to BRB and back changes nothing, an unlinked scene leaves it, Studio Mode preview doesn't count, a scene rename keeps its link.

12. Fixed after the first desktop test: Stream Info and Settings → Highlights maximized (lists / preview take the extra height, rows stay together; Use for scene ▾ no longer under the list), and Test alert for a Twitch sub (and cheer, raid, Super Chat…) leaves a "(test)" line in the dock.

Seen working in the first desktop test (2026-10-13, screenshots): Stream Info reads Twitch, category search with box art, scene links saved; Create group… built the group with its sound; follow and subscriber tests in the dock. YouTube was out of quota for the day (HTTP 403), so Stream Info's YouTube side is untested.

From 2026-10-12:

1. Twitch re-sign-in, then EventSub chat: sending, your own echo, `/me`, refused messages, role badges. (Verified 2026-10-13: the Watching without audio badge shows.)
2. Highlights: alert lines, Settings → Highlights, first-time chatters ✨.
3. Stream Alerts: every new kind's Test button, priority order, minimums, placeholder checks.
4. YouTube: Super Chat and membership lines and alerts; Jewels needs a real gift (check the logged raw figures).
5. Both plugins together: one EventSub session, no doubled events, clean exit during a token refresh.
