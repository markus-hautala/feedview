FeedView - a simple player for NDI(R) video sources
====================================================

Start FeedView, move the mouse to show the controls, and pick a source from the
Source menu. Everything you change (source, volume, audio channels, display) is
remembered for next time.

Keys
  F or F11      fullscreen on / off (double-click the picture does the same)
  Esc           leave fullscreen
  1-9           switch to source 1-9 in the list, 0 = no source
  M             mute
  Up / Down     volume
  I             always show the info line

First start
  Windows  If SmartScreen says "Windows protected your PC", click More info -> Run anyway
           (the app is not code-signed). Allow network access if the firewall asks.
  macOS    Right-click FeedView.app -> Open the first time (the app is not notarized).
           On macOS 15+ allow "Local Network" access, or no sources will be found.
  Linux    NDI discovery needs avahi-daemon running (installed by default on most
           desktops; otherwise: sudo apt install avahi-daemon).

Sources on another subnet/VLAN
  Settings -> Extra discovery IPs: enter the IP of the machine that sends NDI
  (comma separated for several).

Command line
  FeedView --source "STUDIO-PC (Program)" --fullscreen --display 2
  FeedView --list-sources          prints the sources it can see
  FeedView --help                  all options

Test source
  feedview-test-sender publishes colour bars with a moving box and a tone on each
  audio channel (channel 1 = 500 Hz, channel 2 = 1 kHz, ...). Run it on any machine
  on the network and it shows up as "<COMPUTER> (FeedView Test)".
  (macOS: FeedView.app/Contents/MacOS/feedview-test-sender)

NDI(R) is a registered trademark of Vizrt NDI AB. FeedView is not affiliated with
or endorsed by Vizrt NDI AB. See THIRD_PARTY_NOTICES.md for licences.
