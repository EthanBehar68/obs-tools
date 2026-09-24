# Unified Chat for OBS (Twitch + YouTube)

An OBS Studio plugin that adds one dockable chat window showing **Twitch** and **YouTube** live chat together. It's meant for multistreaming with [obs-multi-rtmp](https://github.com/sorayuki/obs-multi-rtmp).

- Every line shows a **platform icon**, then the **display name**, then the **message**.
- One text field at the bottom, next to a **Twitch / YouTube / Both** switch that picks where your message goes.
- Twitch chat is read-only without signing in. Signing in lets you send messages.
- YouTube chat attaches to your current live broadcast automatically, or to a video you choose.
- Sign-in uses the OAuth device-code flow for both platforms: open a URL and type a code. No local web server is involved.

```
┌ Unified Chat ─────────────────────────────────┐
│ [T] Connected  [▶] Connected    Clear Settings │
│ [T] CoolViewer: hello from twitch             │
│ [▶] Jane Doe: hi from youtube!                │
│ [T] you: thanks both                          │
│ [▶] Your Channel: thanks both                 │
├───────────────────────────────────────────────┤
│ Message Twitch and YouTube…     [T][▶][T▶]    │
└───────────────────────────────────────────────┘
```

## Requirements

- OBS Studio **32.2** or newer, Windows x64. The plugin is built against libobs 32.2.1, and OBS refuses to load plugins built for a newer minor version.
- A Twitch application client ID and a Google OAuth client, both free. Setup steps are in [INSTALL.md](INSTALL.md).

## Documentation

- [INSTALL.md](INSTALL.md): install the plugin, create the Twitch and Google credentials, daily use, troubleshooting.
- [BUILDING.md](BUILDING.md): build from source with the toolchain on the drive, run the tests, move the setup to another PC.

## License

GPL-2.0-or-later, like OBS Studio and obs-multi-rtmp. Bundled third-party headers: nlohmann/json (MIT) and doctest (MIT), under `dep/`.

Twitch and YouTube are trademarks of their respective owners. This project isn't affiliated with or endorsed by either.
