# FeedView

A small, fast player for NDI® video sources on Windows, macOS and Linux — built for
confidence monitors, multiview spares and quick checks of what's on the network.

- **Source picker** with live discovery, plus *extra discovery IPs* for sources on other subnets/VLANs.
  **None** is always in the list: a plain black output, the safe choice when nothing is available
- **Fades between sources**: the old picture stays live until the new source is up, then
  dissolves into it (0.5 s by default, or a cut) — the output never flashes black while connecting
- **Video** in its native resolution/frame rate, letterboxed, with alpha shown over black
- **Audio** with channel-pair selection (Ch 1-2, 3-4, …), volume/mute and clock-drift compensation
- **Fullscreen** on any display, borderless; the pointer and controls appear only after the
  mouse has moved for a second (a bumped mouse shows nothing on the output) and hide 2 s after
  it stops
- **Built for live production**: stays on top of every other window, switches OS notifications
  off while it runs, and its volume is the computer's own (with a choice of sound output), so
  the whole show runs from the web remote
- **Live-safe display handling**: survives monitors being unplugged, re-plugged, rearranged or
  changing resolution without a restart; *Identify* (key **D**) puts a big number on every
  screen — click one to move FeedView there
- **Web remote**: open FeedView from a phone or laptop on the same network to see the screen
  layout with the live picture drawn in place, and switch screens, sources and audio
  (select, then *Take*). Built in, no installation, works without internet
- **Bitfocus Companion module** (Stream Deck etc.) with the same controls as the web remote,
  feedbacks (red = on output, green = selected) and ready-made buttons
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

### A zip to share, made on your own computer (Windows)

**Double-click `make-release.cmd`.** It runs all the tests first (about two minutes; FeedView
goes fullscreen and the mouse moves by itself — don't touch them), and only if they all pass
makes **`dist\FeedView-<version>-windows-x64.zip`** and opens the `dist` folder. The zip holds
only what users need, in a `FeedView` folder:

| File | What it is |
|---|---|
| `FeedView.exe` | The app |
| `Processing.NDI.Lib.x64.dll`, `NDI-Processing.NDI.Lib.Licenses.txt` | The NDI runtime and its licence, so users don't have to install anything |
| `README.txt` | First-start notes, keys, the web remote, Companion (`packaging/README-dist.txt`) |
| `THIRD_PARTY_NOTICES.md` | Licences |
| `Companion\feedview-<version>.tgz` | The Bitfocus Companion module (import it on Companion's Modules page) |

No test programs, test files, scripts or source. Before keeping the zip, the script opens it,
refuses anything else, and starts the FeedView inside it as on a computer without NDI
installed, so it has to find the bundled runtime. Options (from a terminal):
`make-release.cmd -WithTestSender` adds the test-pattern sender; `-SkipTests` skips the tests
(not for a real release). The version comes from `project(feedview VERSION …)` in
`CMakeLists.txt`. The NDI runtime is taken from `ndi-runtime\` if you fetched it
(`scripts\fetch-ndi-runtime.ps1 -OutDir ndi-runtime`, as administrator), otherwise from the NDI
Tools installed on your computer. Redistributing it is covered by the NDI SDK licence (see
Licences below). The test `release_package` checks all of this on every test run.

### Releases on GitHub (all three systems)

Push the repo to GitHub. The workflow in `.github/workflows/build.yml` builds all three
platforms on every push, downloads the official NDI runtime, packages it with the app,
and smoke-tests each package against a live test source. Push a tag to publish:

```sh
git tag v1.0.0 && git push origin v1.0.0
```

The three archives, and the Companion module package (`feedview-<version>.tgz`), are attached
to the GitHub Release for that tag. The NDI download URLs
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

### Tests

Every request made for FeedView is listed in [`tests/REQUIREMENTS.md`](tests/REQUIREMENTS.md)
with an ID, and every test scenario that checks one is tagged with it (`[REQ-03] …`). A run
ends with a report of which requests pass (`build/requirements-report.md`); a request with no
passing test fails the run.

**On Windows, double-click `run-tests.cmd`** (or run it in a terminal). It builds FeedView and
the Companion module, runs every test and shows the report. The full run takes over the screen
and the mouse for about two minutes — FeedView goes fullscreen and the mouse moves by itself —
so don't touch them until it's done. Close FeedView first if it runs from `build\Release`.

```powershell
run-tests.cmd                                                           # Windows: everything + report
run-tests.cmd -Quick                                                    # without the desktop tests
powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1          # the same, from PowerShell
bash scripts/run-tests.sh [--quick]                                     # macOS / Linux
```

The tests need CMake 3.21+, a C++ compiler, the NDI runtime, Node.js 22+ (Companion module and
web page tests) and Edge or Chrome (web page test). Claude Code runs the same script after each
of its changes (a Stop hook in `.claude/`) and has to keep everything passing; see `CLAUDE.md`.

| Test | Checks |
|---|---|
| `requirements_coverage` | Every request in `tests/REQUIREMENTS.md` has tagged tests, every tag names a request, and the ways to run the tests are there (a static check; runs everywhere, CI too) |
| `release_package` (Windows) | `scripts/make-release.ps1` with the current build: the zip has only the app, its NDI runtime, README, licences and the Companion module (no test programs, files, scripts or source), and the FeedView in it starts with its bundled runtime; `-WithTestSender` adds the sender |
| `web_remote` (desktop) | FeedView's web page in a real (headless) Edge/Chrome against the real FeedView: every control on the page — sources and None with Take and Cut, screens, Fullscreen, Hide, volume, mute, sound output, channels, Reconnect, every setting, extra IPs — and what each sent and did; fails if a control on the page isn't tested |
| `display_manager` | Display handling against a simulated multi-monitor OS |
| `remote_server` | Web remote HTTP layer: auth, rate limiting, cross-site/DNS-rebinding protection |
| `live_control` | The live-production rules as logic: pointer and controls only after 1 s of mouse movement and gone 2 s after it stops, panels closing when idle or hidden from the web, the fade timing (hold, dissolve, to/from black, cut), no PIN by default (also for 1.0 settings files), sign-in links, the state the web remote gets |
| `os_control` (desktop) | Against this computer: system volume round trip and change notifications, default output, notifications off/on, a window kept above another program's always-on-top window and restored from minimized without taking focus. Everything is put back at once |
| `e2e` (desktop, Windows) | Starts FeedView and test senders and checks each requirement through the web remote API, simulated mouse/keys, Windows and the screen's pixels: no PIN, the 1 s / 2 s pointer rule (also as Windows sees the pointer), None as a plain black output with no text, fades between sources (a dissolve without black, fade in/out, a cut that holds the old picture), the Remote panel's link clicked with the mouse (FeedView reports where its controls are when `FEEDVIEW_TEST_UI` is set), the QR panel on the fullscreen output hidden from the remote (it's gone from the screen's pixels), fullscreen over another always-on-top window and from minimized, staying on top, the system volume/output, notifications off while running and back after, and that the picture never stalls. Takes over the screen for about 50 s |
| `companion_module` | The Companion module, run as Companion runs it (its own process, Companion's IPC protocol) against a stand-in for FeedView's API: every action sends what the web page sends (checked against `web/index.html`, so a new web control without an action fails), select/Take, feedbacks, variables, presets, wrong-PIN and offline handling, and the packaged module |
| `companion_feedview` (desktop) | The Companion module against the real FeedView: every action, with FeedView's state, the variables and the feedbacks checked after each |

The desktop tests skip what a computer can't do (no audio device, no notification permission,
no NDI runtime, no browser) — but in the full run a request whose tests were all skipped counts
as failing, so on the show computer everything really is checked. CI runs the others
(`ctest -LE desktop`). `scripts/run-tests` installs the Companion module's packages on the
first run.

### Test source

`feedview-test-sender` (built alongside the app) sends 75 % colour bars (BT.709) with a
moving box, and a sine tone per audio channel (channel *N* = *N* × 500 Hz) — handy for
checking colours, motion, channel mapping and A/V without a camera:

```sh
./build/feedview-test-sender --name Bars --size 1920x1080 --fps 50 --channels 4
./build/feedview-test-sender --name Key --alpha --size 1280x720 --fps 59.94
./build/feedview-test-sender --name Red --solid FF0000   # one plain colour: handy to watch fades
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

## Live production: the computer is run from the web remote

| What | How |
|---|---|
| Nothing covers the output | **Always on top** (Settings, on by default): FeedView stays above other windows. When fullscreen it also checks twice a second that nothing else (the taskbar, another always-on-top program) has come over it, and moves back on top |
| Fullscreen from the web remote | Comes to the front even if FeedView was behind the active window or minimized, without taking the keyboard from whoever is typing on that computer (FeedView reacts to single keys) |
| No pointer or controls over the picture by accident | The pointer and controls appear only after the mouse has been moving for a second, or on a key (R, M, Up/Down), and hide 2 s after the last input. An open menu or panel stays up to 15 s (its pointer still hides after 2 s, and comes back as soon as the mouse moves). The web remote shows when they're up and can hide them |
| Nothing to show | **None** (first in every source list, key **0**) is a plain black output: no text, no hints, no controls unless the operator moves the mouse at the computer |
| Changing sources | A fade (see below) — the output never goes black while the new source connects |
| No pop-ups | **Silence notifications** (Settings, on by default; Windows): OS notifications are switched off while FeedView runs and back on when it closes |
| Sound | The volume and mute are the computer's own (Windows), and the remote can pick the computer's sound output, which FeedView plays to. Elsewhere FeedView uses its own volume |

Silencing notifications uses the Windows policy *Turn off toast notifications*, which needs a
one-time permission from an administrator: FeedView asks with a Windows prompt (*Allow…* in
Settings or the web remote). After that it switches them off and on by itself, no prompts. If
FeedView is killed, they stay off until FeedView runs and closes again. Windows' own system
messages aren't covered by that policy.

### Fades between sources

| Take | What the output does |
|---|---|
| A source, while a picture is up | The old source keeps playing (picture and sound) until the new one sends its first frame, then they dissolve over the fade time. A new source that sends nothing within 3 s is faded to anyway, i.e. the old picture fades to black |
| **None** | The picture fades out to black |
| A source, from black (None, or no picture) | The new picture fades in when it arrives |
| With the fade set to 0 (a cut) | The old picture still holds until the new one is there, then it cuts |

The fade time is in Settings (FeedView, the web page, Companion), 0.5 s by default. The web
page's Take bar has a **Cut** button next to **Take** for a single take without a fade; the API
takes `fade_ms` with `source`/`reconnect` for the same. Sound crossfades with the picture
(FeedView plays both sources while they're mixed).

## Bitfocus Companion

`companion/feedview` is a [Companion](https://bitfocus.io/companion) module with the same
controls as the web page: sources (with Fade / Cut / a fade time of your own, and None),
select-then-Take, displays and fullscreen, display numbers, system volume, mute, sound
output, channel pair, hiding FeedView's own controls, and every setting. Feedbacks colour the
buttons (red = on the output, green = selected for Take, amber = needs attention), and
variables (`$(feedview:source_name)`, `$(feedview:signal)`, `$(feedview:volume)`, …) can go on
buttons. Presets are built from the sources, displays and sound outputs FeedView reports.

To use it: import the module package `feedview-<version>.tgz` on Companion's **Modules** page
(from a release, or `companion/feedview` after `scripts/run-tests` / `npm run package` there),
then add a **FeedView** connection with the address shown in
FeedView's Remote panel. Leave the PIN empty unless *Require PIN* is on. See
`companion/feedview/companion/HELP.md`.

Developing it: `npm ci`, `npm test`, `npm run package` in `companion/feedview` (Node.js 22).

## Web remote

FeedView serves its own control page on port **8080** (or the next free port up to 8089).
Click **Remote** in FeedView's toolbar (or press **R**) to see the address and a QR code.
Click an address, or press **Enter**, to open the page in this computer's browser; FeedView
then stops staying on top until it's put fullscreen again (or clicked), so the browser isn't
hidden behind it. Scanning the QR code opens the page on a phone. Everything is served by
FeedView itself, so it works on a closed show network with no internet.

The page shows:
- **Your screens** as arranged in the OS, with FeedView's live picture drawn where its
  window is: filling a screen when fullscreen (red label: *On output*), a small rectangle when
  windowed, and a dashed amber outline for a chosen screen that's disconnected.
- A list of the screens with resolution, refresh rate, scaling and which is the main display.
- **Sources** on the network, **signal** details (resolution, frame rate, audio, buffer,
  dropped frames), **audio** controls, recent **events** and **settings**.

Anything that changes what the audience sees is done in two steps: tap a screen or a
source (it turns green), then press **Take** (or **Cut**, for a source without a fade).
**None** is always at the top of the sources. Volume, mute and the sound output act
immediately. **Show display numbers** puts the big Identify numbers on every screen
(projector included) so you can tell them apart. When FeedView's own controls or a panel
(e.g. the Remote panel with its QR code) are on its screen, the page says so and **Hide**
removes them; going fullscreen from the page does that too.

FeedView's own messages ("projector is back…") are never drawn over the fullscreen picture
unless someone is using FeedView's controls at the computer; they're listed under *Events* in
the remote instead. **Clean output** (Settings) goes further: no status text, no dimming,
no idle messages on the fullscreen output at all, so the last good frame simply stays up if
a source drops.

### Security

The remote is meant for a trusted local network; it uses plain HTTP.
- A 4-digit **PIN** can be required: tick *Require PIN* in the Remote panel (it's off by
  default; the PIN is created on first start and can be renewed with *New PIN*). With it,
  the QR code and FeedView's links open the page already signed in. Five wrong guesses from
  one address pause that address for a minute.
- Commands only work with the `X-FeedView-Pin` header, which other web sites can't send, so
  a page open on some other computer can't control FeedView, even without a PIN.
- Without a PIN, requests must use an IP address or a local name (`.local`, `.lan`, the
  computer's name), which blocks DNS-rebinding tricks.
- Turn the remote off in the Remote panel or with `--no-remote`.

First start: Windows asks whether FeedView may use the network. Allow **Private networks**.
macOS asks whether FeedView may accept incoming connections. Click **Allow**.

### HTTP API (for Companion, Stream Deck, scripts)

Commands need the header `X-FeedView-Pin`: with any value (the default, no PIN), or the PIN
if one is required, in which case reading the state needs it too. Commands are `POST` with
form or query parameters and answer `{"ok": true|false, "message": "...", "state": {...}}`.

| Request | Parameters | Does |
|---|---|---|
| `GET /api/ping` | – | App name, version, host, whether a PIN is needed (no auth) |
| `GET /api/state` | – | Everything the page shows, as JSON |
| `GET /api/preview.jpg` | `pin` may be a query parameter | Current picture, 640 px wide (204 if none) |
| `POST /api/fullscreen` | `on=1\|0\|toggle` | Fullscreen on the chosen display / off |
| `POST /api/display` | `number=N` (as on the Identify cards), `fullscreen=1\|0` | Put FeedView on display N |
| `POST /api/identify` | `on=1\|0\|toggle` | Big numbers on every screen |
| `POST /api/source` | `name=MACHINE (Source)`, empty = None (black); optional `fade_ms=0-10000` (0 = cut) for this take | Switch source, with the fade from Settings unless `fade_ms` is given |
| `POST /api/reconnect` | optional `fade_ms` | Reconnect the current source (its picture stays until the new connection is up) |
| `POST /api/volume` | `value=0-100`, or `+5` / `-5` | Volume (the system volume on Windows) |
| `POST /api/mute` | `on=1\|0\|toggle` | Mute (the system mute on Windows) |
| `POST /api/audio-output` | `id=` one of `audio.outputs` in the state | The computer's sound output (Windows) |
| `POST /api/audio-pair` | `first=1\|3\|5…` | Which channel pair to play |
| `POST /api/controls` | `on=0\|1` (default 0) | Hide FeedView's own controls and panels on its screen (or show them) |
| `POST /api/settings` | `clean_output`, `start_fullscreen`, `show_info`, `always_on_top`, `silence_notifications` (`1\|0\|toggle`), `fade_ms` (0-10000), `extra_ips` | Settings |
| `POST /api/allow-notification-control` | – | Ask for the one-time permission to silence notifications (a Windows prompt on the FeedView computer) |

```sh
curl -H "X-FeedView-Pin: x" -d number=2 http://192.168.1.20:8080/api/display
curl -H "X-FeedView-Pin: x" --data-urlencode "name=GFX-PC (Program)" http://192.168.1.20:8080/api/source
curl -H "X-FeedView-Pin: 4821" -d on=0 http://192.168.1.20:8080/api/controls   # with a PIN
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
| `src/overlay_activity.*` | When the pointer and FeedView's own controls are on screen: a second of mouse movement shows them, 2 s without input hides them, panels close when idle, the web remote can hide them. Pure logic, covered by `tests/live_control_test.cpp`. (The pointer is hidden through ImGui, whose SDL backend sets the OS pointer every frame) |
| `src/take_fade.*` | The fade timing of a take: hold the old picture until the new source's first frame (max 3 s), dissolve, fade to/from black. Pure logic, covered by `tests/live_control_test.cpp`; `main.cpp` keeps the old receiver running meanwhile, draws the old picture at 1−a and adds the new one at a (a true dissolve), and mixes the two sound streams. Frames without alpha go into an X texture format so NDI's padding byte can't act as transparency |
| `companion/feedview` | The Bitfocus Companion module (Node.js): `src/api.js` talks to the HTTP API, `actions`/`feedbacks`/`variables`/`presets` are built from FeedView's state. Tests in `companion/feedview/test` |
| `src/os_control.*` | What FeedView controls in Windows: system volume/mute and default sound output (Core Audio, with change notifications so the UI thread never polls devices), notifications (the toast policy), and keeping the output above other windows without taking focus. Covered by `tests/os_control_test.cpp` |
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
