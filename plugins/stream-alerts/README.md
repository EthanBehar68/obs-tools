# Stream Alerts for OBS (Twitch + YouTube)

Follower alerts for **Twitch** and subscriber alerts for **YouTube**, built into OBS: no StreamElements, Streamlabs or browser source. An alert is an OBS source you build yourself (e.g. a group with a media source and a text source). The plugin writes the message into the text source, shows the source for a few seconds, then hides it again.

- **Twitch follows** arrive the moment they happen (EventSub over a WebSocket; the connection sleeps between events).
- **YouTube subscribers** are checked every 2 minutes (30 of your daily 10,000 quota units an hour). Only subscribers who keep their subscriptions public can be seen; that's a YouTube rule.
- Alerts run **only while OBS is streaming**. Nothing connects or polls while you're offline.
- Alerts play **one at a time** with a 1-second gap. Up to **10** wait their turn. Past that, the rest are combined into one alert, e.g. "**+14 more new followers!**" with as many names as fit, so a follow-bot flood or a raid can't take over the screen. Nobody is dropped, and every name is written to the OBS log.
- Each person alerts **once per stream**, so unfollowing and following again doesn't repeat it.

## Requirements

- OBS Studio 32.2 or newer, Windows x64.
- The **OBS Tools** sign-ins, shared with Unified Chat: **Tools → OBS Tools → Accounts…**. See [Unified Chat's INSTALL.md](../unified-chat/INSTALL.md), sections 2 and 3, for creating the Twitch and Google credentials.
  - Twitch: the sign-in must include follower access. If you signed in before this plugin existed, click **Sign in** for Twitch once more.
  - Alerts follow the **signed-in Twitch account's own channel**.

## Install

Close OBS and extract `obs-stream-alerts-<version>-windows-x64.zip` into `C:\ProgramData\obs-studio\plugins\`, or run `scripts\Build.ps1 -Install` (see [BUILDING.md](../../BUILDING.md)).

## Setup

1. **Build the alert in OBS.** For example, add a **group** named `Follow Alert` to your scenes containing:
   - a **Media Source** with your animation or sound (keep "Restart playback when source becomes active" on), and
   - a **Text (GDI+)** source named `Follow Text`.

   **Hide the group** (the eye icon). The plugin shows it for each alert and hides it again. The same source can sit in several scenes; all of its copies are shown.
2. Open **Tools → OBS Tools → Stream Alerts…**. For Twitch follows and YouTube subscribers:
   - **Show source:** the group (or any source or nested scene) to show.
   - **Text source:** the text source that gets the message. Optional.
   - **Message:** `{name}` is replaced by the person's name. Press Enter for a new line (Text (GDI+) shows it as typed).
   - **When more than 10 wait:** `{count}` is replaced by how many more.
   - **Test alert** plays one straight away with the values as entered, so you can try the look without a real follow. Cancel puts the previous settings back.
3. **Show each alert for:** 5 seconds by default.

The **Status** lines show what each side is doing: *Off until you start streaming*, *listening for follows*, *watching for subscribers*, or a problem such as *sign in to Twitch again…*. The same lines appear in the OBS log, prefixed `[obs-stream-alerts]`.

## Where settings are stored

`%APPDATA%\obs-studio\plugin_config\obs-stream-alerts\config.json`. It holds no sign-ins; those are in the shared `plugin_config\obs-tools\accounts.json`.

## License

GPL-2.0-or-later, like OBS Studio.
