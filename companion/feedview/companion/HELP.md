## FeedView

Controls FeedView, the NDI player for confidence monitors and projection, through its
built-in web remote. Everything FeedView's own web page does is here as actions,
feedbacks, variables and presets.

### Setup

1. In FeedView press **R** (or click **Remote**) to see its address, e.g. `http://192.168.1.20:8080`.
2. Add a **FeedView** connection in Companion and enter that address (or just the IP and port).
3. Leave the **PIN** empty, unless _Require PIN_ is ticked in FeedView's Remote panel.

Use an IP address or the computer's local name (`studio-pc`, `studio-pc.local`): without a PIN
FeedView refuses other host names. With a wrong PIN the module stops asking (FeedView pauses an
address after five wrong PINs) and waits until the PIN is fixed.

### What's in it

- **Sources**: put a source on the output, with the fade set in FeedView, a cut, or a fade time
  of your own. **None** is a plain black output, the safe choice when no source is available.
  The old picture stays until the new source is ready, so nothing flashes black.
- **Select, then Take**, as on the web page: select a source or a display (green), then **Take**
  (or **Cut**). Presets for both ways are built from the sources FeedView sees.
- **Displays**: put FeedView fullscreen (or a window) on a display, fullscreen on/off, show the
  display numbers on every screen.
- **Audio**: volume (the computer's system volume on Windows), up/down, mute, the computer's
  sound output, and which channel pair of the source to play.
- **FeedView's own controls**: hide them (also closes panels like the Remote panel's QR code).
- **Settings**: clean output, start in fullscreen, info line, always on top, silence
  notifications, fade time, extra discovery IPs, and the one-time permission FeedView needs
  to silence Windows notifications.

Feedbacks: red = on the output, green = selected for Take, amber = needs attention (signal
lost, no picture yet, FeedView's controls showing, waiting for a disconnected display, muted).

Variables (`$(feedview:...)`): `source_name`, `signal` (Live, Connecting, No video, ...),
`resolution`, `fps`, `volume`, `muted`, `sound_output`, `display`, `fullscreen`, `fade_ms`,
`selected`, `last_event`, and more; see the connection's Variables tab.

Not here: the live picture (the web page's preview) and the event list.
