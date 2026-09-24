# Installing and setting up Unified Chat

## 1. Install the plugin

Requires OBS Studio **32.2 or newer** (64-bit Windows). Check with **Help → About**.

1. Close OBS.
2. Install it with either option:
   - **From the release zip:** extract `obs-unified-chat-1.0.0-windows-x64.zip` into `C:\ProgramData\obs-studio\plugins\`. You should end up with
     `C:\ProgramData\obs-studio\plugins\obs-unified-chat\bin\64bit\obs-unified-chat.dll`.
   - **From source:** run `scripts\Build.ps1 -Install` (see [BUILDING.md](BUILDING.md)).
3. Start OBS and open **Docks → Unified Chat**. Drag the dock wherever you like; OBS remembers its position.

This is the same plugin folder obs-multi-rtmp uses, so the two sit side by side.

To uninstall, close OBS and delete `C:\ProgramData\obs-studio\plugins\obs-unified-chat`.

## 2. Twitch setup (one time)

Reading Twitch chat only needs a channel name. Sending messages needs a sign-in, which requires your own Twitch application:

1. Go to <https://dev.twitch.tv/console/apps> and click **Register Your Application**. Twitch requires two-factor authentication on your account for this.
2. Fill in:
   - **Name:** anything unique, e.g. `yourname-obs-chat`
   - **OAuth Redirect URLs:** `http://localhost` (the device flow never uses it, but the field is required)
   - **Category:** Chat Bot
   - **Client Type:** **Public**
3. Click **Create**, then **Manage**, and copy the **Client ID**.
4. In OBS, open **Unified Chat → Settings**:
   - **Channel:** your channel name, or a `twitch.tv/...` URL
   - **Client ID:** paste it
   - Click **Sign in**. Your browser opens `twitch.tv/activate` and the code is copied to your clipboard. Approve the request.
   - The dialog shows **Signed in as ...**. Click **OK**.

The plugin keeps the sign-in refreshed automatically and validates it hourly, as Twitch requires. If you sign in with a different account than the channel (e.g. a bot account), messages are sent from that account.

## 3. YouTube setup (one time)

YouTube requires a Google Cloud project with the YouTube Data API enabled, for both reading and sending.

1. Open <https://console.cloud.google.com/>, create a project (e.g. `obs-chat`) and select it.
2. **APIs & Services → Library:** search for **YouTube Data API v3** and click **Enable**.
3. **Google Auth Platform** (called **OAuth consent screen** in older layouts):
   - **User type:** External. **App name:** anything. **Support email:** your email.
   - Under **Audience → Test users**, add the Google account that owns your YouTube channel.
   - Recommended: click **Publish app** so the status is **In production**. While the app is in *Testing*, Google expires the sign-in every 7 days. For a personal app you don't need Google's verification; you'll just see an "unverified app" warning when signing in, which you can continue past.
4. **Clients → Create client:**
   - **Application type:** **TVs and Limited Input devices**
   - Create it and copy the **Client ID** and **Client secret**.
5. In OBS, open **Unified Chat → Settings**, paste both values into the YouTube section and click **Sign in**. Your browser opens `google.com/device` and the code is copied to your clipboard. Choose the account (and the brand channel, if you use one), then allow access.

### How the YouTube chat gets picked

- **Video field empty (recommended):** the plugin finds your **currently live** broadcast. With obs-multi-rtmp pushing to your YouTube stream key, the broadcast goes live a few seconds after you start that output. Until then the dock shows *YouTube: waiting for a live broadcast* and checks again every 30 seconds.
- **Video field set:** paste a watch URL, `youtu.be` link, `youtube.com/live/...` link, a YouTube Studio URL, or an 11-character video ID. Use this for scheduled or unlisted streams, or to follow a stream on another channel you have access to.

### YouTube API quota

Google gives each project a free daily quota (10,000 units by default). Every chat poll uses part of it, so the **Minimum poll interval** setting trades message latency against how long your quota lasts:

| Interval | Rough streaming hours per day |
|----------|-------------------------------|
| 5 s      | ~3 h                          |
| 8 s (default) | ~4.5 h                   |
| 15 s     | ~8 h                          |

These are estimates. Your actual usage is under **APIs & Services → YouTube Data API v3 → Quotas** in the Cloud Console. When the quota runs out, the dock says so and pauses YouTube for 15 minutes. Quota resets at midnight Pacific time. Sending a message costs more than a poll but is rare by comparison.

## 4. Using the dock

- **Status buttons** (top left) show each platform's state: *Connected*, *Read-only* (Twitch without sign-in), *Connecting…*, or *Offline*. Click one to open Settings.
- **Target switch** (bottom right): Twitch, YouTube, or both. The choice is saved.
- Press **Enter** to send. Your own messages appear right away.
  - Twitch allows 500 characters and YouTube allows 200. If a message is too long for any selected platform, nothing is sent and the text stays in the box so you can shorten it.
  - If one selected platform is disconnected, the message still goes to the other and the dock notes which one was skipped.
  - `/me waves` sends a Twitch action (shown in italics).
- **Clear** empties the view. **Messages to keep** in Settings caps how much history the dock holds (default 500).
- System notices (connection changes, subscriptions and raids, errors) appear as grey italic lines.

## 5. Where settings are stored

`%APPDATA%\obs-studio\plugin_config\obs-unified-chat\config.json`

This file holds your channel, client IDs and sign-in tokens in plain text, the same way OBS stores stream keys. Don't share it. **Sign out** in Settings removes the stored token. To revoke access completely, also disconnect the app at <https://www.twitch.tv/settings/connections> or <https://myaccount.google.com/permissions>.

## 6. Troubleshooting

Every plugin log line starts with `[obs-unified-chat]`. Check **Help → Log Files → View Current Log**.

| Symptom | Fix |
|---------|-----|
| No **Unified Chat** entry under Docks | Look in the log for `compiled with newer libobs`: update OBS to 32.2 or newer. Otherwise check the DLL path from step 1. |
| `Twitch: no channel configured` | Enter a channel in Settings. |
| `Twitch: login rejected` / `sign-in expired` | Click **Sign in** again. Chat keeps working read-only in the meantime. |
| `could not start sign-in (invalid client)` | Wrong client ID, or for Twitch the app's client type isn't **Public**. |
| YouTube sign-in fails with `invalid_client` or `unauthorized_client` | The Google client type must be **TVs and Limited Input devices**, and the secret must belong to that client. |
| YouTube sign-in blocked with "access denied" | Add your account under **Test users**, or publish the app (step 3). |
| `YouTube: waiting for a live broadcast` while you're live | YouTube can take up to a minute to mark the broadcast live. If it never connects, paste the stream URL into **Video**. |
| `YouTube: API quota exceeded` | Raise the poll interval, or wait for the daily reset. |
| `YouTube: request failed (HTTP 403, forbidden)` | The signed-in account doesn't own or moderate that live chat. |
