// Minimal test helpers shared by FeedView's C++ tests.
//
//   SCENARIO("[REQ-03] fullscreen from the remote ...");   starts a scenario
//   CHECK(cond);                                            counts a failure if false
//   SKIP("why");                                            the scenario couldn't run here
//   return finish();                                        at the end of main()
//
// Each scenario ends with a line "RESULT PASS|FAIL|SKIP <name>". The [REQ-xx] tags in the
// names are the requests in tests/REQUIREMENTS.md; scripts/requirements-report.mjs collects
// these lines into the report of which requests pass.
#pragma once

#include <cstdio>
#include <string>

namespace check {
inline int failures = 0, checks = 0, skipped = 0;
inline std::string current;
inline int failuresAtStart = 0;
inline bool skippedNow = false;

inline void end() {
    if (current.empty()) return;
    const char* r = failures > failuresAtStart ? "FAIL" : skippedNow ? "SKIP" : "PASS";
    std::printf("RESULT %s %s\n", r, current.c_str());
    std::fflush(stdout);
    current.clear();
}

inline void begin(const char* name) {
    end();
    current = name;
    failuresAtStart = failures;
    skippedNow = false;
    std::printf("- %s\n", name);
    std::fflush(stdout);
}

inline void skip(const char* why) {
    ++skipped;
    skippedNow = true;
    std::printf("  SKIPPED: %s\n", why);
    std::fflush(stdout);
}

inline int finish() {
    end();
    std::printf("%d checks, %d failed, %d skipped\n", checks, failures, skipped);
    return failures ? 1 : 0;
}
}  // namespace check

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++check::checks;                                                   \
        if (!(cond)) {                                                     \
            ++check::failures;                                             \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::fflush(stdout);                                           \
        }                                                                  \
    } while (0)
#define SCENARIO(name) check::begin(name)
#define SKIP(why) check::skip(why)
using check::finish;
