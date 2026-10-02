# FeedView

A small, fast player for NDI® video sources on Windows, macOS and Linux — built for
confidence monitors, multiview spares and quick checks of what's on the network.

- **Source picker** with live discovery, plus *extra discovery IPs* for sources on other subnets/VLANs
- **Video** in its native resolution/frame rate, letterboxed, with alpha shown over black
- **Audio** with channel-pair selection (Ch 1-2, 3-4, …), volume/mute and clock-drift compensation
- **Fullscreen** on any display, borderless; controls and pointer hide after 3 s
- Clear **signal-lost** state (dimmed last frame + red label) and automatic reconnection
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

## How it works

| File | Role |
|---|---|
| `src/ndi_runtime.*` | Finds and loads the NDI runtime at run time and resolves each function by name, so any NDI 5/6 runtime works (6.1 doesn't export the `NDIlib_v6_load` table newer headers declare) |
| `src/ndi_io.*` | `SourceFinder` (discovery thread) and `Receiver` (capture thread). Video is requested as BGRA so NDI does the YUV→RGB conversion with the correct BT.601/709 matrix and every renderer can upload it directly |
| `src/main.cpp` | SDL3 window/renderer/audio, the ImGui overlay, fullscreen, settings and CLI. Audio goes through an SDL audio stream that resamples any source rate; a small controller nudges the playback rate (±0.5 % max) to cancel clock drift between sender and sound card |
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
