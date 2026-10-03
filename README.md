# FeedView

A small, fast player for NDI® video sources on Windows, macOS and Linux — built for
confidence monitors, multiview spares and quick checks of what's on the network.

- **Source picker** with live discovery, plus *extra discovery IPs* for sources on other subnets/VLANs
- **Video** in its native resolution/frame rate, letterboxed, with alpha shown over black
- **Audio** with channel-pair selection (Ch 1-2, 3-4, …), volume/mute and clock-drift compensation
- **Fullscreen** on any display, borderless; controls and pointer hide after 3 s
- **Live-safe display handling**: survives monitors being unplugged, re-plugged, rearranged or
  changing resolution without a restart; *Identify* (key **D**) puts a big number on every
  screen — click one to move FeedView there
- **Web remote**: open FeedView from a phone or laptop on the same network to see the screen
  layout with the live picture drawn in place, and switch screens, sources and audio
  (select, then *Take*). Built in, no installation, works without internet
- Clear **signal-lost** state (dimmed last frame + red label) and automatic reconnection;
  *Clean output* keeps the projected picture free of any FeedView text
- Remembers source, volume, channel pair, display and start-in-fullscreen between runs
- Scriptable: `--source`, `--fullscreen`, `--display`, `--list-sources`, …

The release downloads bundle the NDI runtime, so users just unzip and run.

## For end users

Download the archive for your OS from the [Releases](../../releases) page (or a CI run's
artifacts) and unzip it. See [`packaging/README-dist.txt`](packaging/README-dist.txt) for
first-start notes (SmartScreen, Gatekeeper, macOS Local Network permission, avahi on Linux),
keys and command-line options.

| OS | Download | Runtime inside |
|---|---|---|
| Windows 10/11 x64 | `FeedView-windows-x64.zip` | `Processing.NDI.Lib.x64.dll` next to `FeedView.exe` |
| macOS 11+ (Apple Silicon + Intel) | `FeedView-macos-universal.zip` | `FeedView.app/Contents/Frameworks/libndi.dylib` |
| Linux x86_64 (Ubuntu 24.04+ class) | `FeedView-linux-x86_64.tar.gz` | `lib/libndi.so.6` |

If the bundled runtime is missing, FeedView falls back to an installed NDI Runtime/NDI Tools
(`NDI_RUNTIME_DIR_V6`, `/usr/local/lib`, `/Library/NDI SDK for Apple`) and otherwise shows a
screen with a download link and a *Try again* button.

## Making releases

Push the repo to GitHub. The workflow in `.github/workflows/build.yml` builds all three
platforms on every push, downloads the official NDI runtime, packages it with the app,
and smoke-tests each package against a live test source. Push a tag to publish:

```sh
git tag v1.0.0 && git push origin v1.0.0
```

The three archives are attached to the GitHub Release for that tag. The NDI download URLs
can be overridden with the `NDI_SDK_LINUX_URL`, `NDI_RUNTIME_MAC_URL` and `NDI_RUNTIME_WIN_URL`
environment variables if NDI moves them.

The builds are not code-signed. For wider distribution, sign the Windows exe and sign +
notarize the macOS app with your own certificates (add the steps after *Package*).

## Building locally

Needs CMake 3.21+ and a C++20 compiler. SDL3 and Dear ImGui are fetched automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On Linux install SDL's build dependencies first (the list is in the workflow). To run a
local build, either install the NDI runtime or fetch it next to the binary:

```sh
bash scripts/fetch-ndi-runtime.sh build/lib   # Linux: build/lib/libndi.so.6
./build/FeedView
```

`FEEDVIEW_NDI_LIBRARY=/path/to/libndi` forces a specific runtime file.

### Test source

`feedview-test-sender` (built alongside the app) sends 75 % colour bars (BT.709) with a
moving box, and a sine tone per audio channel (channel *N* = *N* × 500 Hz) — handy for
checking colours, motion, channel mapping and A/V without a camera:

```sh
./build/feedview-test-sender --name Bars --size 1920x1080 --fps 50 --channels 4
./build/feedview-test-sender --name Key --alpha --size 1280x720 --fps 59.94
```

## Display behaviour during a live event

Displays are numbered left to right. The chosen display is remembered by its name and its
position among same-named monitors — never by an OS index, which changes whenever something
is plugged in. When the arrangement changes, FeedView waits ~0.75 s for the burst of OS
events to settle, then:

| What happens | What FeedView does |
|---|---|
| Target screen unplugged / powered off | Leaves fullscreen and waits in a window ("Waiting for …") instead of covering whatever screen the OS moved it to |
| Target screen comes back (even with a new OS id) | Goes fullscreen on it again and says so |
| Screens rearranged or resolution changed | Re-fits the fullscreen window |
| Operator leaves fullscreen via the OS (green button, Win+↓) | Respected — FeedView doesn't fight it |
| OS kicks it out of fullscreen during a display change | Puts it back |
| A screen never reports the exact size | Gives up after 3 tries for that arrangement (no flicker loop) |
| Windowed FeedView left off-screen | Moved back onto a visible screen |

Quick fixes: **D** (Identify, then click a card or press its number), **Ctrl/Cmd+1–9**, the
display menu, or **F** while waiting = fullscreen right here. The UI rescales when the window
moves to a monitor with a different scaling factor. On macOS FeedView uses instant
(non-Spaces) fullscreen so switching never animates or jumps to another Space.

## Web remote

FeedView serves its own control page on port **8080** (or the next free port up to 8089).
Click **Remote** in FeedView's toolbar to see the address, a QR code and the PIN. Scanning
the QR code opens the page already signed in. Everything is served by FeedView itself, so
it works on a closed show network with no internet.

The page shows:
- **Your screens** as arranged in the OS, with FeedView's live picture drawn where its
  window is: filling a screen when fullscreen (red label: *On output*), a small rectangle when
  windowed, and a dashed amber outline for a chosen screen that's disconnected.
- A list of the screens with resolution, refresh rate, scaling and which is the main display.
- **Sources** on the network, **signal** details (resolution, frame rate, audio, buffer,
  dropped frames), **audio** controls, recent **events** and **settings**.

Anything that changes what the audience sees is done in two steps: tap a screen or a
source (it turns green), then press **Take**. Volume and mute act immediately.
**Show display numbers** puts the big Identify numbers on every screen (projector
included) so you can tell them apart.

FeedView's own messages ("projector is back…") are never drawn over the fullscreen picture
unless someone is using FeedView's controls at the computer; they're listed under *Events* in
the remote instead. **Clean output** (Settings) goes further: no status text, no dimming,
no idle messages on the fullscreen output at all, so the last good frame simply stays up if
a source drops.

### Security

The remote is meant for a trusted local network; it uses plain HTTP.
- A 4-digit **PIN** is required by default (it's created on first start and can be renewed
  with *New PIN*). Five wrong guesses from one address pause that address for a minute.
- Commands only work with the `X-FeedView-Pin` header, which other web sites can't send, so
  a page open on some other computer can't control FeedView, even without a PIN.
- Without a PIN, requests must use an IP address or a local name (`.local`, `.lan`, the
  computer's name), which blocks DNS-rebinding tricks.
- Turn the remote off in the Remote panel or with `--no-remote`.

First start: Windows asks whether FeedView may use the network. Allow **Private networks**.
macOS asks whether FeedView may accept incoming connections. Click **Allow**.

### HTTP API (for Companion, Stream Deck, scripts)

All `/api` calls except `ping` need the header `X-FeedView-Pin: <PIN>` (any value if the PIN
is turned off). Commands are `POST` with form or query parameters and answer
`{"ok": true|false, "message": "...", "state": {...}}`.

| Request | Parameters | Does |
|---|---|---|
| `GET /api/ping` | – | App name, version, host, whether a PIN is needed (no auth) |
| `GET /api/state` | – | Everything the page shows, as JSON |
| `GET /api/preview.jpg` | `pin` may be a query parameter | Current picture, 640 px wide (204 if none) |
| `POST /api/fullscreen` | `on=1\|0\|toggle` | Fullscreen on the chosen display / off |
| `POST /api/display` | `number=N` (as on the Identify cards), `fullscreen=1\|0` | Put FeedView on display N |
| `POST /api/identify` | `on=1\|0\|toggle` | Big numbers on every screen |
| `POST /api/source` | `name=MACHINE (Source)`; empty = no source | Switch source |
| `POST /api/reconnect` | – | Reconnect the current source |
| `POST /api/volume` | `value=0-100`, or `+5` / `-5` | Volume |
| `POST /api/mute` | `on=1\|0\|toggle` | Mute |
| `POST /api/audio-pair` | `first=1\|3\|5…` | Which channel pair to play |
| `POST /api/settings` | `clean_output`, `start_fullscreen`, `show_info` (`1\|0`), `extra_ips` | Settings |

```sh
curl -H "X-FeedView-Pin: 4821" -d number=2 http://192.168.1.20:8080/api/display
curl -H "X-FeedView-Pin: 4821" --data-urlencode "name=GFX-PC (Program)" http://192.168.1.20:8080/api/source
```

## How it works

| File | Role |
|---|---|
| `src/ndi_runtime.*` | Finds and loads the NDI runtime at run time and resolves each function by name, so any NDI 5/6 runtime works (6.1 doesn't export the `NDIlib_v6_load` table newer headers declare) |
| `src/ndi_io.*` | `SourceFinder` (discovery thread) and `Receiver` (capture thread). Video is requested as BGRA so NDI does the YUV→RGB conversion with the correct BT.601/709 matrix and every renderer can upload it directly |
| `src/main.cpp` | SDL3 window/renderer/audio, the ImGui overlay, fullscreen, settings and CLI. Audio goes through an SDL audio stream that resamples any source rate; a small controller nudges the playback rate (±0.5 % max) to cancel clock drift between sender and sound card |
| `src/display_manager.*` | Which screen FeedView is on and how it reacts to display changes. Pure logic with no SDL, covered by `tests/display_manager_test.cpp`, which replays unplug/replug/rearrange/resolution/identical-monitor scenarios against a simulated OS (`ctest`) |
| `src/remote_server.*` | The web remote's HTTP server ([cpp-httplib](https://github.com/yhirose/cpp-httplib)): auth, rate limiting, cross-site and DNS-rebinding protection. Commands are queued and run on the UI thread, which publishes state and preview frames, so phones never stall the picture. Covered by `tests/remote_server_test.cpp` |
| `src/remote_view.*`, `web/index.html` | The state JSON, preview downscaling, and the control page (embedded into the binary at build time) |
| `src/settings.*` | `settings.ini` in the per-user app data folder |
| `tools/test_sender.cpp` | The test-pattern sender |

Settings live in `%APPDATA%\FeedView\FeedView\settings.ini` (Windows),
`~/Library/Application Support/FeedView/FeedView/settings.ini` (macOS) and
`~/.local/share/FeedView/FeedView/settings.ini` (Linux).

## Licences

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Redistributing the NDI runtime is
governed by the NDI SDK License Agreement (<https://ndi.link/ndisdk_license>), which among
other things covers trademark use and a commercial-use threshold — check it before shipping
FeedView commercially. NDI® is a registered trademark of Vizrt NDI AB.
