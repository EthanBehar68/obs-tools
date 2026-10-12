# Stream Alerts for OBS (Twitch + YouTube)

Alerts for **Twitch** and **YouTube**, built into OBS: no StreamElements, Streamlabs or browser source. An alert is an OBS source you build yourself (e.g. a group with a media source and a text source). The plugin writes the message into the text source, shows the source for a few seconds, then hides it again.

| Platform | Alerts | How they arrive |
|---|---|---|
| Twitch | follows, subs and resubs, gift subs, cheers, raids | the moment they happen (EventSub over one WebSocket; the connection sleeps between events) |
| YouTube | channel subscribers | checked every 2 minutes (30 of your daily 10,000 quota units an hour). Only public subscriptions can be seen; that's a YouTube rule |
| YouTube | Super Chats and Super Stickers, new members and milestones, gifted memberships | from [Unified Chat](../unified-chat/README.md)'s YouTube chat, about 2 seconds after they happen, at no extra quota. Unified Chat must be installed and connected to YouTube |

- Alerts run **only while OBS is streaming**. Nothing connects or polls while you're offline.
- Alerts play **one at a time** with a 1-second gap.
- **Follows and subscribers:** up to **10** wait their turn. Past that, the rest are combined into one alert, e.g. "**+14 more new followers!**" with as many names as fit, so a follow-bot flood can't take over the screen. Nobody is dropped, and every name is written to the OBS log. Each person alerts **once per stream**, so unfollowing and following again doesn't repeat it.
- **Subs, gifts, cheers, raids, Super Chats and memberships** always play, each on its own and every time, ahead of any waiting follows and subscribers (never interrupting the alert on screen). Cheers and raids can have a minimum (bits / viewers).
- A gift of several subs at once is **one** alert ("gifted 5 subs"), not one per recipient. A resub alerts when the viewer shares it in chat; that's how Twitch reports resubs.
- An alert kind with no source set is skipped, so a kind you haven't built yet never holds up the others.
- With **Unified Chat** installed, each new follower and subscriber also appears in the chat dock as a highlighted line, straight away, even when their on-screen alert is still waiting or combined. Subs, cheers, raids and Super Chats already show in the chat.

## Requirements

- OBS Studio 32.2 or newer, Windows x64.
- The **OBS Tools** sign-ins, shared with Unified Chat: **Tools → OBS Tools → Accounts…**. See [Unified Chat's INSTALL.md](../unified-chat/INSTALL.md), sections 2 and 3, for creating the Twitch and Google credentials.
  - Twitch: the sign-in must include follower, chat and bits access. If you signed in before this version, click **Sign in** for Twitch once more; until then the status says which alerts are missing.
  - Alerts follow the **signed-in Twitch account's own channel**.

## Install

Close OBS and extract `obs-stream-alerts-<version>-windows-x64.zip` into `C:\ProgramData\obs-studio\plugins\`, or run `scripts\Build.ps1 -Install` (see [BUILDING.md](../../BUILDING.md)).

## Setup

1. **Build the alert in OBS.** For example, add a **group** named `Follow Alert` to your scenes containing:
   - a **Media Source** with your animation or sound (keep "Restart playback when source becomes active" on), and
   - a **Text (GDI+)** source named `Follow Text`.

   **Hide the group** (the eye icon). The plugin shows it for each alert and hides it again. The same source can sit in several scenes; all of its copies are shown.
2. Open **Tools → OBS Tools → Stream Alerts…**. Pick an alert kind on the left, then:
   - **Show source:** the group (or any source or nested scene) to show.
   - **Text source:** the text source that gets the message. Optional.
   - **Message:** the placeholders under it are replaced, e.g. `{name}`, `{months}`, `{count}`, `{amount}`, `{viewers}`, `{message}`, `{level}`. Press Enter for a new line (Text (GDI+) shows it as typed). Subs and members have a second message for resubs and milestones.
   - **When more than 10 wait** (follows and subscribers): `{count}` is replaced by how many more.
   - **Only alert from** (cheers and raids): the smallest cheer in bits, or raid in viewers, that alerts.
   - **Test alert** plays one straight away with the values as entered and made-up details, so you can try the look without a real event. Cancel puts the previous settings back.

   Each kind can use its own group, or several can share one.
3. **Show each alert for:** 5 seconds by default.

The **Status** line shows what that kind's side is doing: *Off until you start streaming*, *listening for follows, subs, gifts, raids, cheers*, *watching for subscribers*, or a problem such as *sign in to Twitch again…*. The same lines appear in the OBS log, prefixed `[obs-stream-alerts]`.

## Where settings are stored

`%APPDATA%\obs-studio\plugin_config\obs-stream-alerts\config.json`. It holds no sign-ins; those are in the shared `plugin_config\obs-tools\accounts.json`.

## License

GPL-2.0-or-later, like OBS Studio.
