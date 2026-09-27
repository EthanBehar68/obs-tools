# Changelog

## 1.2.0 (2026-09-26)

### Emotes and badges (Twitch)
- **Emotes show as pictures:** Twitch's own emotes, plus your channel's and the global **BTTV, FFZ and 7TV** emotes. Animated emotes show their first frame, which keeps the dock light during streams. Emotes are a little taller than the text and keep their real shape.
- **Role badges** (broadcaster, moderator, VIP, subscriber/founder) appear before names. They need a Twitch sign-in.
- Images download once per session in the background. Lines appear at once with the right spacing and the picture fills in, so the chat doesn't jump.
- In your own sent messages, BTTV/FFZ/7TV emotes show as pictures; Twitch's own emotes stay as words (Twitch doesn't send the positions back).

### Settings
- **Settings is on tabs** (Twitch, YouTube, General, Legend), so the dialog is no longer very tall. The Legend explains badges and emotes too.

## 1.1.0 (2026-09-26)

### Chat
- **YouTube chat arrives in about 2 seconds.** It's streamed from YouTube instead of polled. Polling is still available in Settings → YouTube → Chat delivery, and streaming switches to it by itself if it stops working.
- **YouTube chat connects when you press Start Streaming** and waits ("Waiting for stream") while you're offline, using no quota. The *Always* option keeps the old behaviour.
- **Old YouTube chat is no longer replayed** when OBS goes live on a reused broadcast.
- **Readable name colours:** chatters' colours are lightened just enough to read on the dock's background (brand colours as the fallback).
- **Your own lines are marked with a gold star**, including YouTube messages you type in Studio or on your phone.
- **Bot lines are merged:** when a bot (Nightbot by default; set it in Settings → General) posts the same text on both platforms within 30 seconds, you see one line with both icons.

### Tagging people
- **Click a name** to mention that person. The target switches to their platform for that message and returns to your choice after sending.
- **Twitch replies:** clicking a Twitch name also threads your message under theirs.
- **Type `@` and a letter** for a list of matching recent chatters. Tab / ↑ / ↓ / Enter to pick.
- **Messages that mention you are highlighted.**

### Moderation
- **Deleted messages, timeouts, bans and chat clears stay visible:** struck through, with a coloured tag (deleted / timed out / banned / chat cleared) and a notice. YouTube only reports bans, not single deletions.
- **Settings → Legend** explains every mark the dock uses.

### Performance and security
- **A much smaller footprint:** one reused connection to YouTube instead of a new one every poll, and batched chat updates. The Twitch thread sleeps until there's something to do, and YouTube downloads only the fields the plugin uses.
- **Saved sign-ins are encrypted** for your Windows account. Existing settings are converted automatically.

### Docs
- INSTALL.md: corrected YouTube quota figures (a poll costs 1 unit, a message 50, a stream 5), a streaming vs polling table, and the multi-rtmp "Sync start with OBS" setup.

## 1.0.0 (2026-09-23)
- First release: Twitch and YouTube chat in one OBS dock, with one input box and a Twitch / YouTube / Both send switch.
- A message sent to Both shows as one line with both icons.
- Device-code sign-in for Twitch and YouTube.
