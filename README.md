# obs-tools

OBS Studio plugins for streaming to Twitch and YouTube at the same time, built from one repository.

| Plugin | What it does |
|--------|--------------|
| [Unified Chat](plugins/unified-chat/README.md) (`obs-unified-chat`) | One dock with Twitch and YouTube live chat together: send to either or both, emotes, badges, moderation, viewer counts. |
| [Stream Alerts](plugins/stream-alerts/README.md) (`obs-stream-alerts`) | Twitch follower and YouTube subscriber alerts that show an OBS source you build; no browser source or third-party service. |

Each plugin is a separate DLL with its own version and settings, and installs and updates on its own. They share one set of Twitch and YouTube sign-ins (**Tools → OBS Tools → Accounts…**) and tested code in `libs/common` (OAuth device sign-in, the shared accounts file, HTTP over libcurl, encryption, text helpers).

## Layout

```
libs\common\        shared code: tested core, networking, the shared Accounts window
libs\obs-support\   per-plugin obs_log / PLUGIN_NAME / PLUGIN_VERSION template
plugins\<name>\     one folder per plugin: plugin.json (name, version), src, data, tests, docs
dep\                vendored nlohmann/json and doctest
cmake\ scripts\     build system (obs-plugintemplate based) and Build.ps1
```

[BUILDING.md](BUILDING.md) covers building, testing and packaging all plugins.

## License

GPL-2.0-or-later, like OBS Studio. Bundled third-party headers: nlohmann/json (MIT) and doctest (MIT), under `dep/`.

Twitch and YouTube are trademarks of their respective owners. This project isn't affiliated with or endorsed by either.
