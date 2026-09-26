# Building Unified Chat from source

Everything needed to build is in the `OBS-Dev` folder on the drive. The only thing that has to be installed on the PC is Visual Studio 2022 Build Tools, and an offline installer for it is included.

## What's on the drive

```
OBS-Dev\
├── obs-unified-chat\          this repository
│   ├── .deps\                 downloaded OBS 32.2.1 sources, obs-deps and Qt 6 (prebuilt; no internet needed)
│   ├── build_x64\             CMake build tree (regenerated automatically on a new PC)
│   └── release\               output of Build.ps1 -Package
├── tools\
│   ├── cmake\                 portable CMake 4.4.3
│   ├── git\                   portable Git for Windows 2.55
│   └── vs2022-buildtools\
│       └── layout\            offline installer: MSVC v143 (14.44) + Windows 11 SDK 10.0.26100
└── reference\                 obs-plugintemplate and obs-multi-rtmp sources, for conventions
```

## Moving to another PC (desktop)

1. Plug in the drive, or copy the whole `OBS-Dev` folder to any disk. The drive letter doesn't have to be `D:`.
2. Open **PowerShell as Administrator** in `OBS-Dev\obs-unified-chat` and run once:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\scripts\Setup-BuildMachine.ps1
   ```
   This installs Visual Studio 2022 Build Tools from the offline layout (skipped if already installed) and marks the repository as a safe git directory.
3. Close OBS, then build, test and install in a normal (non-admin) PowerShell:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\scripts\Build.ps1 -Install
   ```
   - If the folder path, drive letter or compiler location changed since the last build, the script reconfigures from scratch. That includes rebuilding libobs from `.deps`, which takes about 8 to 10 minutes. Later builds take seconds.
   - `-Package` also writes `release\obs-unified-chat-<version>-windows-x64.zip` for installing on other machines.
   - `-Configuration Debug` builds a debug version. `-SkipTests` skips the unit tests.

To use Git, put the portable copy on your PATH for the session:

```powershell
$env:PATH = "$PWD\..\tools\git\cmd;$PWD\..\tools\cmake\bin;$env:PATH"
git log --oneline
```

## Building by hand

Plain CMake presets, same as the OBS plugin template and obs-multi-rtmp:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64          # RelWithDebInfo
ctest --preset windows-x64                  # unit tests
cmake --install build_x64 --config RelWithDebInfo   # -> C:\ProgramData\obs-studio\plugins
```

You can also open `build_x64\obs-unified-chat.sln` in Visual Studio 2022.

### Debugging inside OBS

Install a `Debug` or `RelWithDebInfo` build, then attach the Visual Studio debugger to `obs64.exe`. The `.pdb` is installed next to the DLL.

## Tests

The unit tests live in `tests\` and use [doctest](https://github.com/doctest/doctest), vendored in `dep\doctest`. They cover the libobs-free core:

| Area | File |
|------|------|
| Twitch IRC parsing, tag unescaping, login/PING/RECONNECT state machine, `/me`, CRLF injection | `test-twitch-irc.cpp` |
| YouTube response parsing, video URL parsing, dedupe, and the full `ChatSession` against a scripted fake HTTP server (broadcast discovery, paging, token refresh, 401 retry, quota backoff, chat end, send + echo) | `test-youtube.cpp` |
| OAuth device flow request bodies and Twitch/Google response mapping | `test-oauth.cpp` |
| Send routing (targets, length limits, partial delivery), HTML escaping of chat lines, config round trip | `test-router-format-config.cpp` |
| Text helpers (UTF-8 length, escaping, URL encoding) | `test-text-util.cpp` |

Run them with `ctest --preset windows-x64`, or run `build_x64\RelWithDebInfo\unified-chat-tests.exe` directly (it accepts doctest options such as `-tc="*YouTube*"`). The tests need no network, OBS or Qt.

## Project layout

```
src\
├── core\          platform logic: no libobs, no Qt, no I/O  -> static library unified-chat-core
│   ├── twitch-irc.*      IRC parser + IrcSession state machine
│   ├── youtube-api.*     Data API v3 parsing + ChatSession (drives an injected HttpClient); streams chat
│   │                     via liveChat/messages/stream by default, falls back to polling
│   ├── json-array-reader.*  splits the chunked JSON array of a chat stream into complete objects
│   ├── oauth-device.*    RFC 8628 device flow for Twitch and Google
│   ├── chat-router.*     Twitch / YouTube / Both routing and length rules
│   ├── chat-format.*     chat line -> escaped Qt rich text
│   ├── name-color.*      readable name colors (WCAG contrast vs. the chat background), cached per color
│   ├── echo-merger.*     your own Both message shown as one line
│   ├── bot-merger.*      a bot's identical Twitch + YouTube lines folded into one (line shown first, icon added later)
│   └── chat-config.*     config.json (de)serialization
├── net\           worker threads on the libcurl that ships with OBS
│   ├── twitch-connection.*   TLS IRC to irc.chat.twitch.tv:6697 via CURLOPT_CONNECT_ONLY
│   ├── youtube-connection.*  YouTube poll/send loop
│   ├── device-login.*        sign-in flow
│   └── curl-http-client.*
├── chat-dock.*        the dock widget
├── settings-dialog.*
├── target-switch.*    the Twitch / YouTube / Both switch
├── platform-icons.*   icons painted with QPainter (no image assets)
└── plugin-main.cpp    module entry, dock registration (obs_frontend_add_dock_by_id)
```

### Design notes

- **Smallest possible CPU and memory footprint. This rule decides between implementations.** The plugin runs while OBS encodes one or more streams (obs-multi-rtmp) and a game is running, so every cycle it spends is taken from them. Do nothing per message that can be done once; cache derived values; avoid polling, busy waits and redundant UI work; prefer the cheaper approach whenever the user can't see the difference.
- **Networking uses libcurl, not Qt Network.** The Qt that ships with OBS has no TLS backend plugin, so `QSslSocket` and HTTPS through `QNetworkAccessManager` fail at runtime. OBS itself (rtmp-services, the updater) uses its bundled `libcurl.dll` with Schannel, and so does this plugin.
- **Threads never touch widgets.** Each connection runs on its own `std::thread`, and callbacks marshal to the UI with `QMetaObject::invokeMethod(dock, ..., Qt::QueuedConnection)`. Shutdown happens on `OBS_FRONTEND_EVENT_EXIT`: every blocking wait is interruptible, and curl transfers abort through a progress callback, so OBS closes promptly.
- **All chat text is HTML-escaped** before it reaches the `QTextBrowser`, and Twitch colors must match `#rrggbb`. Outgoing text has CR/LF stripped so a message can't inject extra IRC commands.
- **libobs version pin.** `buildspec.json` pins OBS 32.2.1, the same as obs-multi-rtmp. OBS rejects plugins built against a newer libobs minor version, so the plugin loads on OBS 32.2.x and later.

### Conventions followed

- Build system: [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate), i.e. `buildspec.json`, `CMakePresets.json`, `cmake/`, `.github/` CI workflows, and `data/locale/en-US.ini` with `obs_module_text`.
- C++20 with Qt 6 Widgets, as in obs-multi-rtmp: kebab-case file names, PascalCase types and methods, `member_` fields, `obs_frontend_add_dock_by_id`, config saved on `OBS_FRONTEND_EVENT_EXIT`.
- Formatting: the template's `.clang-format` (OBS style: tabs, 120 columns). Run the clang-format 19 that ships with VS Build Tools:
  ```powershell
  & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-format.exe" -i (Get-ChildItem src,tests -Recurse -Include *.cpp,*.hpp)
  ```

## FAT32 / exFAT drive notes

The SanDisk drive is formatted FAT32, which needs two workarounds. Both are already in the repo:

1. **Archive timestamps:** the obs-deps archives contain timestamps FAT32 can't store, so extraction failed with `Can't restore time`. `cmake/common/buildspec_common.cmake` extracts with `TOUCH` and applies the same patch to the downloaded OBS sources, following the same pattern obs-multi-rtmp uses for its macOS Swift patch.
2. **Relinking:** `link.exe` can't overwrite its previous output on FAT32 (`LNK1105 ... error code 1224`). `CMakeLists.txt` deletes the old binary in a `PRE_LINK` step.

FAT32 also can't hold files larger than 4 GB. Nothing in this project comes close; the largest file is the 260 MB Qt archive.

If you copy `OBS-Dev` to an NTFS disk on the desktop, everything keeps working the same.
