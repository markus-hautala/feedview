FeedView - a simple player for NDI(R) video sources
====================================================

Start FeedView, move the mouse for a second to show the controls, and pick a source
from the Source menu. Until then the window is black: that's "None", the safe black
output. Everything you change (source, audio channels, display) is remembered for
next time.

Keys
  F or F11      fullscreen on / off (double-click the picture does the same)
  Esc           leave fullscreen
  1-9           switch to source 1-9 in the list, 0 = None (black)
  D             show display numbers on every screen; click one, or press its
                number, to put FeedView fullscreen there (Esc cancels)
  Ctrl+1-9      fullscreen on display 1-9 directly (Cmd+1-9 on a Mac)
  M             mute
  Up / Down     volume
  I             always show the info line
  R             web remote address and QR code (Enter opens it in the browser)

Screens are numbered from left to right, as arranged in the OS display settings.

Live production
  - The pointer and controls appear only after the mouse has moved for a second, so a
    bumped mouse shows nothing on the output, and hide 2 s after the mouse stops.
    Open menus close after 15 s.
  - None (top of every source list, key 0) is a plain black output with no text: the
    safe choice when no source is available.
  - Sources change with a fade: the old picture stays until the new source is up, then
    dissolves into it, so the output never flashes black. Settings -> Fade between
    sources (0.5 s by default, 0 = cut).
  - Always on top (Settings, on): nothing covers FeedView; fullscreen from the web
    remote comes to the front even from behind other windows or minimized, without
    taking the keyboard from whoever is using the computer.
  - Silence notifications (Settings, on, Windows): OS notifications are off while
    FeedView runs. The first time, click Allow... and confirm the Windows prompt
    (needs an administrator, once).
  - Windows: the volume and mute are the computer's own, and Settings or the web
    remote choose the sound output.

When screens change during an event
  Nothing needs restarting. FeedView remembers its screen by name, not by number:
  - Projector/monitor unplugged or switched off: FeedView drops to a normal window
    (so it never covers your own screen) and shows "Waiting for <screen>".
    When the screen comes back, FeedView goes fullscreen on it again by itself.
  - Screens rearranged, re-ordered or resolution changed: the picture is re-fitted
    automatically.
  - Wrong screen? Press D and click the right one. Or pick it from the display
    menu next to the Fullscreen button.
  - Want it on the screen it's on now instead? Press F ("Fullscreen here").
  - Start in fullscreen (Settings) + a screen that isn't on yet: FeedView waits for
    it and goes fullscreen as soon as it appears.

Web remote (control FeedView from a phone or another computer)
  Click Remote in FeedView's toolbar (or press R). Scan the QR code with a phone on
  the same network, type the address shown there (e.g. http://192.168.1.20:8080), or
  click it to open it in this computer's browser.
  The page shows your screens as arranged, with FeedView's live picture drawn on the
  screen it's on. Tap a screen or a source, then press Take (or Cut: no fade). Volume,
  mute and the sound output act right away. "Show display numbers" puts big numbers on every
  screen. If FeedView's controls or a panel are on its screen, "Hide" removes them.
  Settings -> Clean output keeps all FeedView text off the projected picture; status
  and events are then only in the web remote.
  No PIN is needed by default: anyone on your network can use it. Tick Require PIN in
  the Remote panel to need one, or turn the remote off there.

Bitfocus Companion (Stream Deck and other control surfaces)
  The FeedView module (feedview-<version>.tgz, with the releases) has the same controls
  as the web remote. Import it on Companion's Modules page, add a FeedView connection
  and enter the address from FeedView's Remote panel.

First start
  Windows  If SmartScreen says "Windows protected your PC", click More info -> Run anyway
           (the app is not code-signed). When the firewall asks, allow FeedView on
           Private networks (needed for NDI and the web remote).
  macOS    Right-click FeedView.app -> Open the first time (the app is not notarized).
           On macOS 15+ allow "Local Network" access, or no sources will be found, and
           allow incoming connections for the web remote.
  Linux    NDI discovery needs avahi-daemon running (installed by default on most
           desktops; otherwise: sudo apt install avahi-daemon).

Sources on another subnet/VLAN
  Settings -> Extra discovery IPs: enter the IP of the machine that sends NDI
  (comma separated for several).

Command line
  FeedView --source "STUDIO-PC (Program)" --fullscreen --display 2   (2nd screen from left)
  FeedView --list-sources          prints the sources it can see
  FeedView --remote-port 9000      web remote on another port (--no-remote turns it off)
  FeedView --help                  all options

Test source
  feedview-test-sender publishes colour bars with a moving box and a tone on each
  audio channel (channel 1 = 500 Hz, channel 2 = 1 kHz, ...). Run it on any machine
  on the network and it shows up as "<COMPUTER> (FeedView Test)".
  (macOS: FeedView.app/Contents/MacOS/feedview-test-sender)

NDI(R) is a registered trademark of Vizrt NDI AB. FeedView is not affiliated with
or endorsed by Vizrt NDI AB. See THIRD_PARTY_NOTICES.md for licences.
