# FeedView: your requests, and the tests that check them

Every request you made is listed here with an ID. Each test scenario that checks one is
tagged with that ID (e.g. `[REQ-03]`), and the test run ends with a report that lists every
request as **PASS** / **FAIL**: a request fails if any of its scenarios fails, or if no
scenario checked it in that run (a request without a test can't slip through).

Run them: double-click **`run-tests.cmd`** in the FeedView folder (or run
`scripts\run-tests.ps1`). It builds FeedView and the Companion module, runs every test and
writes the report to `build\requirements-report.md`. The full run takes over the screen and
the mouse for about two minutes: don't touch them meanwhile. Claude runs the same tests after
each change and may only finish when they all pass.

| ID | Your request | Checked by |
|---|---|---|
| REQ-01 | "I can click the link in the application that follows me to the web browser instead of writing it manually." | e2e: the address in FeedView's Remote panel is clicked with the mouse (and opened with Enter) and the browser is asked to open it; unit: the link signs in when a PIN is required |
| REQ-02 | "The application shall be on top of everything" | os_control: kept above another program's always-on-top window without taking the keyboard; e2e: FeedView is above other windows and the taskbar, and comes back on top within a second when another program pushes in front |
| REQ-03 | "when I click the fullscreen in the web page, it goes to full screen even if it is under other active windows" | e2e: fullscreen from the remote while another always-on-top program covers FeedView, and while FeedView is minimized; web: the page's Fullscreen button |
| REQ-04 | "if the remote qr code dialog was opened, it was visible in full screen and was not able to unhide using web buttons" (it must be possible to hide it from the web page) | e2e: the QR panel opened on the fullscreen output is hidden from the remote, and going fullscreen from the remote closes it; web: the page's Hide button; unit: panels close by themselves after 15 s |
| REQ-05 | "Also all OS notifications shall be hidden during the application is active." (administrator rights may be used) | os_control: notifications off and back on; e2e: off while FeedView runs, back as they were when it closes |
| REQ-06 | "This is used in live production. So, the idea is that I can control everything over web page." | web: every control on the page is used in a real browser against FeedView, and FeedView follows; e2e: the commands behind them |
| REQ-07 | "Volume controls the global OS volume." | os_control: the system volume, read back by another reader; e2e: the remote's volume is the system volume; web: the page's volume slider |
| REQ-08 | "Audio destination of the OS can be selected also." | os_control: the default sound output; e2e and web: choosing it from the remote |
| REQ-09 | "PIN request is by default turned off." | unit: the default, also for settings files from FeedView 1.0; e2e: a fresh FeedView needs no PIN; web: the page opens without asking for one |
| REQ-10 | "Overlays are good, but require mouse moving at least for one second to active those." | unit: the 1 s rule; e2e: half a second of movement shows nothing, a second does |
| REQ-11 | "Create test cases based from these requests and run those always after any change." | coverage: every request here has tests; the Claude Code hook that runs them after every change is set up |
| REQ-12 | "Hide mouse cursor after 2 sec inactivity" | unit: 2 s; e2e: hidden 2 s after the mouse stops, as Windows itself reports the pointer |
| REQ-13 | "and unhide it after 1sec activity." | unit: 1 s; e2e: back after a second of moving, as Windows reports it |
| REQ-14 | "When no output selected, show black screen, no info texts visible." | e2e: the screen's pixels are all black with None: on a fresh start, fullscreen, with the info line switched on, while FeedView has a message to show, and after a bump of the mouse |
| REQ-15 | "The idea is to have a safe "none" input to select when no input available. Show this "None" input in the source list always, even there are some ndi sources." | web: None is first in the page's list while NDI sources are listed, and taking it gives a black output; Companion: None is first in every source list; e2e: None from the remote |
| REQ-16 | "Implement a companion bitfocus module." | Companion: the module starts as Companion runs it, connects, and the packaged module works the same |
| REQ-17 | "Cover the same controls into that module as there are in the web page." | Companion: every command and setting the web page uses has an action (checked against the page's code), each sends what the page sends, and every action works against the real FeedView |
| REQ-18 | "Change the input with fade" | unit: the fade timing; e2e: a dissolve between two sources with no black in between, fade in from and out to black, and a cut that holds the old picture (screen pixels); web: the fade setting and Cut; Companion: fade/cut per take |
| REQ-19 | "include all the requests by me in this conversation into test cases. These test cases must pass always after each change by Claude." | coverage: this list is complete and every request has tests; the hook runs them after each change and keeps Claude working until they pass; CLAUDE.md tells every Claude session the rule |
| REQ-20 | "The user can also run the test cases" | coverage: `run-tests.cmd` and the scripts are there; every run writes this report |
| REQ-21 | "how to create such a release package that it does not contain all the test related files and I can share it as a zip packet?" | release_package: `make-release.cmd` makes `dist\FeedView-<version>-windows-x64.zip` with only the app, its NDI runtime, README, licences and the Companion module — no test programs, test files or source — and the FeedView inside starts with its own bundled NDI runtime |
