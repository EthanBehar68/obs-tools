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

Google gives each project a free daily quota (10,000 units by default). Per [Google's quota table](https://developers.google.com/youtube/v3/determine_quota_cost), each chat poll costs **1 unit** and each message you send to YouTube costs **50 units**, including messages sent with the switch on Both. Finding your broadcast and your channel name costs 1 unit each, once per connection.

**Chat delivery** (Settings → YouTube) chooses how messages arrive:

- **Streaming (the default).** YouTube pushes each message about 2 seconds after it's posted. The server ends each stream after about 10 seconds and the plugin reopens it at once. Each stream costs **5 units** (measured; streams aren't listed in Google's table), so about **1,700 units an hour** while you're live. Sending a message briefly interrupts the stream so the message goes out immediately, which adds one extra stream (5 units) per message. If streaming stops working, the plugin switches to polling for the rest of that broadcast and says so in the dock.
- **Polling.** The plugin asks for new messages every **Minimum poll interval**. It's cheaper, but messages appear up to one interval late.

| Delivery | Delay after a message is posted | Units per hour | Hours per day, reading only | Hours per day, also sending 10 YouTube messages an hour |
|---|---|---|---|---|
| Streaming (default) | ~2 s | ~1,700 | ~5.9 h | ~4.4 h |
| Polling every 3 s | ~2 s + up to 3 s | 1,200 | ~8 h | ~5.9 h |
| Polling every 5 s | ~2 s + up to 5 s | 720 | ~14 h | ~8.2 h |
| Polling every 8 s | ~2 s + up to 8 s | 450 | ~22 h | ~10.5 h |
| Polling every 15 s | ~2 s + up to 15 s | 240 | all day | ~13.5 h |

If your streams are longer than streaming's budget allows, switch to polling, or ask Google for a higher quota (free, through the Cloud Console quota page). When polling, YouTube may ask for a longer interval than yours, and the plugin always waits at least as long as YouTube asks. Your actual usage is under **APIs & Services → YouTube Data API v3 → Quotas** in the Cloud Console. When the quota runs out, the dock says so and pauses YouTube for 15 minutes. Quota resets at midnight Pacific time.

### When YouTube chat connects

By default (**Settings → YouTube → Connect chat: When OBS starts streaming**) YouTube chat connects when you press OBS's **Start Streaming** and disconnects when you stop. While you're offline it uses no quota, and the YouTube status reads **Waiting for stream**. Right after you start, it checks for your broadcast every 10 seconds for a minute, since YouTube needs a few seconds to bring it live, then every 30 seconds. Twitch chat stays connected all the time.

This needs YouTube to go live from OBS's own **Start Streaming** button, which is also what makes OBS's YouTube integration create and start the broadcast. To start Twitch (or other destinations) from the same button with obs-multi-rtmp, click **Modify** on that target and tick **Sync start with OBS** and **Sync stop with OBS**.

If YouTube goes live some other way (another app, or a multistream output with an auto-start stream key), choose **Always**. The plugin then checks for a live broadcast every 2 minutes while you're offline (about 30 units an hour).

## 4. Using the dock

- **Status buttons** (top left) show each platform's state: *Connected*, *Read-only* (Twitch without sign-in), *Connecting…*, or *Offline*. Click one to open Settings.
- **Target switch** (bottom right): Twitch, YouTube, or both. The choice is saved.
- Press **Enter** to send. A message sent to both platforms shows as **one line with both icons** once both have accepted it (about a second).
  If your Twitch and YouTube names differ it shows `TwitchName / YouTubeName`. If one platform rejects the message, only the other icon is shown.
  - Twitch allows 500 characters and YouTube allows 200. If a message is too long for any selected platform, nothing is sent and the text stays in the box so you can shorten it.
  - If one selected platform is disconnected, the message still goes to the other and the dock notes which one was skipped.
  - `/me waves` sends a Twitch action (shown in italics).
- **Tagging people:**
  - **Click a name** in the chat to start a message to that person. `@name ` goes into the input, and the target switches to their platform **for that message**, so a Twitch name never goes to YouTube and vice versa. After you send (or clear the input), the target goes back to the one you chose. If you click the target switch yourself in the meantime, your pick stays.
  - For a **Twitch** message, the reply is threaded under that message on Twitch, and a *Replying to @name* bar appears above the input. Click **✕** on the bar to send a plain mention instead. YouTube has no replies, so there it's a plain `@handle` mention.
  - **Type `@` and a letter** and a list of matching people who've chatted recently appears above the input, each with its platform icon (up to 8, most recent first). Use **↑/↓** to choose, **Enter** or **Tab** to pick (Enter picks a name here, it doesn't send), **Esc** to close, or click a name. Picking a name switches to that person's platform for the message, like clicking their name in chat. On Both the list shows people from both platforms; otherwise only the current platform.
  - With just `@` typed (no letter), **Tab** cycles through recent chatters instead.
  - Twitch replies from others show who they answer: `Viewer → @Dezad: welcome back`.
  - **Messages that mention you** (your Twitch login or YouTube name) get a tinted background.
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
| `YouTube: API quota exceeded` | Send fewer messages to YouTube (each costs 50 polls' worth), raise the poll interval, or wait for the daily reset. |
| `YouTube: request failed (HTTP 403, forbidden)` | The signed-in account doesn't own or moderate that live chat. |
