# FeedView: rules for Claude

FeedView runs live production output. Every request the user has made is listed in
`tests/REQUIREMENTS.md` and covered by tests; they must keep passing.

- **After every change, run all tests**: `powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1`
  (builds FeedView and the Companion module, runs every test incl. the desktop/end-to-end ones,
  then `scripts/requirements-report.mjs --strict`). Don't report work as done until it passes:
  every request in `tests/REQUIREMENTS.md` must be **PASS** (skipped or not tested counts as
  failing). A Stop hook (`.claude/hooks/run-tests-if-changed.ps1`) also runs it when code changed.
- **A new request from the user** gets a row in `tests/REQUIREMENTS.md` (next free `REQ-xx`,
  their words quoted) and test scenarios tagged `[REQ-xx]` in their names, in the test that can
  really show it: `tests/live_control_test.cpp` (logic), `tests/os_control_test.cpp` (Windows),
  `tests/e2e_test.cpp` (the real app: screen pixels, mouse, keyboard), `tests/web/web_remote.test.mjs`
  (the web page in a browser), `companion/feedview/test/*.js` (the Companion module).
  `requirements_coverage` fails if a request has no tagged scenario.
- **A new control on the web page** needs a Companion action too (`companion/feedview`); the
  module's tests and the web page test both check every command in `web/index.html` is covered.
- The full run takes over the screen and mouse for ~2 minutes; a FeedView running from
  `build/Release` locks the exe - ask the user before closing it.
- Live-production rules: nothing of FeedView's may appear on the output by accident, the
  picture must never stall (`frameGapMs` in the state), FeedView must not take keyboard focus.
