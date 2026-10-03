#!/usr/bin/env bash
# Builds FeedView and the Companion module, runs the tests and reports which of your requests
# (tests/REQUIREMENTS.md) pass (macOS / Linux). Several requests are about Windows (system
# volume, notifications, staying on top) and are only tested there; --quick leaves out the
# desktop tests. The report is written to <build>/requirements-report.md.
#
#   bash scripts/run-tests.sh [--quick] [build-dir]
set -uo pipefail
cd "$(dirname "$0")/.."

quick=0
if [ "${1:-}" = "--quick" ]; then quick=1; shift; fi
build="${1:-build}"

# Companion module: packages (first run) and the importable package
if command -v node > /dev/null; then
  (cd companion/feedview && { [ -d node_modules/@companion-module/base ] || npm ci --no-audit --no-fund; } && npm run --silent package) || exit 1
else
  echo "Node.js not found: the Companion module and the web page can't be tested."
fi

cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release > /dev/null || exit 1
cmake --build "$build" --config Release --parallel || exit 1
rm -f "$build/test-results.xml"
if [ "$quick" = 1 ]; then
  ctest --test-dir "$build" -C Release --output-on-failure --output-junit test-results.xml -LE desktop
else
  ctest --test-dir "$build" -C Release --output-on-failure --output-junit test-results.xml
fi
tests=$?
report=0
if command -v node > /dev/null; then
  # Not strict here: the Windows-only requests can't pass on this system.
  node scripts/requirements-report.mjs "$build/test-results.xml"
  report=$?
fi
[ "$tests" -ne 0 ] && exit "$tests"
exit "$report"
