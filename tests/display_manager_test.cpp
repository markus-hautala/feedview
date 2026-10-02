// Scenario tests for DisplayManager against a simulated multi-monitor OS.
// Build: part of the CMake project (FEEDVIEW_BUILD_TESTS), run with ctest.

#include "display_manager.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static int gFailures = 0;
static int gChecks = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        ++gChecks;                                                         \
        if (!(cond)) {                                                     \
            ++gFailures;                                                   \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

// A pretend operating system with monitors and one application window.
struct FakeOS : DisplayPlatform {
    std::vector<DisplayInfo> ds;
    bool fs = false;
    DisplayRect win{100, 100, 960, 540};
    DisplayRect windowed{100, 100, 960, 540};
    std::vector<std::string> notes;
    int enterCalls = 0, leaveCalls = 0, moveCalls = 0;
    int shrinkOnFullscreen = 0;  // simulate a platform that never gives the exact size

    const DisplayInfo* find(uint32_t id) const {
        for (const auto& d : ds)
            if (d.id == id) return &d;
        return nullptr;
    }
    std::vector<DisplayInfo> displays() override {
        // OS enumeration order deliberately differs from screen order.
        std::vector<DisplayInfo> v(ds.rbegin(), ds.rend());
        return v;
    }
    bool windowFullscreen() override { return fs; }
    uint32_t windowDisplay() override {
        int cx = win.x + win.w / 2, cy = win.y + win.h / 2;
        for (const auto& d : ds)
            if (cx >= d.bounds.x && cx < d.bounds.x + d.bounds.w && cy >= d.bounds.y && cy < d.bounds.y + d.bounds.h)
                return d.id;
        return 0;
    }
    DisplayRect windowRect() override { return win; }
    void enterFullscreen(uint32_t id) override {
        ++enterCalls;
        const DisplayInfo* d = find(id);
        if (!d) return;
        if (!fs) windowed = win;
        fs = true;
        win = d->bounds;
        win.w -= shrinkOnFullscreen;
    }
    void leaveFullscreen() override {
        ++leaveCalls;
        fs = false;
        win = windowed;
    }
    void moveWindowTo(uint32_t id) override {
        ++moveCalls;
        const DisplayInfo* d = find(id);
        if (!d) return;
        fs = false;
        win = {d->bounds.x + (d->bounds.w - windowed.w) / 2, d->bounds.y + (d->bounds.h - windowed.h) / 2,
               windowed.w, windowed.h};
        windowed = win;
    }
    void notify(const std::string& m) override { notes.push_back(m); }

    // ---- OS-side events
    void unplug(uint32_t id, bool osKeepsFullscreen) {
        const bool wasOn = windowDisplay() == id;
        for (size_t i = 0; i < ds.size(); ++i)
            if (ds[i].id == id) ds.erase(ds.begin() + long(i));
        if (wasOn && !ds.empty()) {
            if (fs && osKeepsFullscreen) {
                win = ds[0].bounds;  // e.g. Windows moves the borderless window to the primary
            } else if (fs) {
                fs = false;          // other platforms drop out of fullscreen
                win = {ds[0].bounds.x + 50, ds[0].bounds.y + 50, windowed.w, windowed.h};
            }
            // A windowed window may simply be left where it was (now off-screen).
        }
    }
    void plug(DisplayInfo d) { ds.push_back(d); }
    void setBounds(uint32_t id, DisplayRect r) {
        for (auto& d : ds)
            if (d.id == id) d.bounds = r;
    }
    bool fullscreenOn(uint32_t id) const {
        const DisplayInfo* d = find(id);
        return d && fs && win.x == d->bounds.x && win.y == d->bounds.y && win.w == d->bounds.w &&
               win.h == d->bounds.h;
    }
    bool onScreen() const {
        for (const auto& d : ds) {
            int ow = std::min(win.x + win.w, d.bounds.x + d.bounds.w) - std::max(win.x, d.bounds.x);
            int oh = std::min(win.y + win.h, d.bounds.y + d.bounds.h) - std::max(win.y, d.bounds.y);
            if (ow > 100 && oh > 50) return true;
        }
        return false;
    }
};

static DisplayInfo disp(uint32_t id, const char* name, int x, int y, int w, int h) {
    DisplayInfo d;
    d.id = id;
    d.name = name;
    d.bounds = {x, y, w, h};
    return d;
}

// Runs the manager's clock forward, like frames passing.
static void run(DisplayManager& m, uint64_t& now, uint64_t ms) {
    for (uint64_t t = 0; t < ms; t += 16) {
        now += 16;
        m.update(now);
    }
}

static void laptopAndProjector(FakeOS& os) {
    os.ds = {disp(1, "Built-in Retina", 0, 0, 1512, 982), disp(2, "EPSON PJ", 1512, 0, 1920, 1080)};
    os.win = os.windowed = {200, 200, 960, 540};
}

#define SCENARIO(name) std::printf("- %s\n", name)

int main() {
    {
        SCENARIO("numbering is left to right, not OS order; identical models get their own number");
        FakeOS os;
        os.ds = {disp(7, "DELL U2720Q", 2560, 0, 2560, 1440), disp(3, "DELL U2720Q", 0, 0, 2560, 1440),
                 disp(9, "LG HDR 4K", 5120, 0, 3840, 2160)};
        auto a = arrangeDisplays(os.displays());
        CHECK(a[0].id == 3 && a[1].id == 7 && a[2].id == 9);
        CHECK(a[0].nth == 0 && a[1].nth == 1 && a[2].nth == 0);
        CHECK(findDisplay(a, {"DELL U2720Q", 1, -1}) == 1);
        CHECK(findDisplay(a, {"Projector", 0, -1}) == -1);
    }
    {
        SCENARIO("start fullscreen on the remembered projector");
        FakeOS os;
        laptopAndProjector(os);
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        CHECK(os.fullscreenOn(2));
        CHECK(m.targetIndex() == 1);
    }
    for (bool osKeepsFullscreen : {true, false}) {
        SCENARIO(osKeepsFullscreen ? "projector unplugged (OS keeps window fullscreen on laptop) and replugged"
                                   : "projector unplugged (OS drops fullscreen) and replugged");
        FakeOS os;
        laptopAndProjector(os);
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        run(m, now, 2000);

        os.unplug(2, osKeepsFullscreen);
        m.displaysChanged(now);
        m.windowDisplayChanged(now);
        if (!osKeepsFullscreen) m.windowLeftFullscreen(now);
        run(m, now, 2000);
        CHECK(!os.fs);  // must not cover the operator's laptop screen
        CHECK(os.onScreen());
        CHECK(m.waitingForTarget());
        CHECK(m.wantFullscreen());
        CHECK(!os.notes.empty() && os.notes.back().find("disconnected") != std::string::npos);

        // Projector comes back (new id after re-plug, same name).
        os.plug(disp(5, "EPSON PJ", 1512, 0, 1920, 1080));
        m.displaysChanged(now);
        run(m, now, 2000);
        CHECK(os.fullscreenOn(5));
        CHECK(!m.waitingForTarget());
        CHECK(os.notes.back().find("is back") != std::string::npos);
    }
    {
        SCENARIO("projector not connected at start-up: wait, then go fullscreen when it appears");
        FakeOS os;
        os.ds = {disp(1, "Built-in Retina", 0, 0, 1512, 982)};
        os.win = os.windowed = {200, 200, 960, 540};
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        run(m, now, 3000);
        CHECK(!os.fs);
        CHECK(m.waitingForTarget());
        os.plug(disp(2, "EPSON PJ", 1512, 0, 1920, 1080));
        m.displaysChanged(now);
        run(m, now, 2000);
        CHECK(os.fullscreenOn(2));
    }
    {
        SCENARIO("no display event at all (platform missed it): watchdog still recovers");
        FakeOS os;
        os.ds = {disp(1, "Built-in Retina", 0, 0, 1512, 982)};
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        os.plug(disp(2, "EPSON PJ", 1512, 0, 1920, 1080));
        run(m, now, 2500);
        CHECK(os.fullscreenOn(2));
    }
    {
        SCENARIO("monitors rearranged: projector moves to the left of the laptop");
        FakeOS os;
        laptopAndProjector(os);
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        run(m, now, 2000);
        os.setBounds(2, {-1920, 0, 1920, 1080});
        m.displaysChanged(now);
        run(m, now, 2000);
        CHECK(os.fullscreenOn(2));
        CHECK(m.targetIndex() == 0);  // now display 1 (leftmost), still the projector
    }
    {
        SCENARIO("projector resolution changes 1080p -> 4K: fullscreen window re-fitted");
        FakeOS os;
        laptopAndProjector(os);
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        run(m, now, 2000);
        os.setBounds(2, {1512, 0, 3840, 2160});
        m.displaysChanged(now);
        run(m, now, 2000);
        CHECK(os.fullscreenOn(2));
        CHECK(os.win.w == 3840);
    }
    {
        SCENARIO("operator leaves fullscreen via the OS: respected, not fought");
        FakeOS os;
        laptopAndProjector(os);
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        run(m, now, 5000);
        os.leaveFullscreen();  // e.g. Win+Down or the macOS green button
        m.windowLeftFullscreen(now);
        run(m, now, 5000);
        CHECK(!os.fs);
        CHECK(!m.wantFullscreen());
    }
    {
        SCENARIO("windowed window dragged to the projector becomes the fullscreen target");
        FakeOS os;
        laptopAndProjector(os);
        DisplayManager m(os);
        uint64_t now = 1000;
        m.start(now, false);
        run(m, now, 5000);
        os.win = {2000, 200, 960, 540};  // dragged onto the projector
        m.windowDisplayChanged(now);
        m.setFullscreen(true, now);
        CHECK(os.fullscreenOn(2));
        CHECK(m.target().name == "EPSON PJ");
    }
    {
        SCENARIO("windowed window left off-screen after its monitor is unplugged is brought back");
        FakeOS os;
        laptopAndProjector(os);
        DisplayManager m(os);
        uint64_t now = 1000;
        m.start(now, false);
        os.win = os.windowed = {2000, 200, 960, 540};
        m.windowDisplayChanged(now);
        run(m, now, 4000);
        os.unplug(2, false);
        m.displaysChanged(now);
        run(m, now, 2000);
        CHECK(os.onScreen());
        CHECK(!os.fs);
    }
    {
        SCENARIO("F while waiting for a missing screen = fullscreen here, and that becomes the target");
        FakeOS os;
        os.ds = {disp(1, "Built-in Retina", 0, 0, 1512, 982)};
        os.win = os.windowed = {200, 200, 960, 540};
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        run(m, now, 2000);
        m.toggleFullscreen(now);  // the F key
        CHECK(os.fullscreenOn(1));
        CHECK(m.target().name == "Built-in Retina");
        CHECK(!m.waitingForTarget());
    }
    {
        SCENARIO("Esc while waiting for a missing screen cancels the wait");
        FakeOS os;
        os.ds = {disp(1, "Built-in Retina", 0, 0, 1512, 982)};
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        run(m, now, 2000);
        m.setFullscreen(false, now);  // Esc
        CHECK(!m.waitingForTarget() && !m.wantFullscreen());
        os.plug(disp(2, "EPSON PJ", 1512, 0, 1920, 1080));
        m.displaysChanged(now);
        run(m, now, 3000);
        CHECK(!os.fs);  // the operator cancelled; it must not jump to the projector later
    }
    {
        SCENARIO("choose display by number / next display, fullscreen and windowed");
        FakeOS os;
        os.ds = {disp(1, "A", 0, 0, 1920, 1080), disp(2, "B", 1920, 0, 1920, 1080), disp(3, "C", 3840, 0, 1280, 720)};
        DisplayManager m(os);
        uint64_t now = 1000;
        m.start(now, false);
        m.chooseDisplay(2, true, now);  // Ctrl+3 / Identify click
        CHECK(os.fullscreenOn(3));
        run(m, now, 2000);
        m.nextDisplay(now);  // wraps to display 1, stays fullscreen
        CHECK(os.fullscreenOn(1));
        run(m, now, 2000);
        m.setFullscreen(false, now);
        m.chooseDisplay(1, false, now);  // windowed move
        CHECK(!os.fs && os.windowDisplay() == 2);
        m.chooseDisplay(7, true, now);  // no such display
        CHECK(os.notes.back() == "There is no display 8");
    }
    {
        SCENARIO("two identical monitors: the chosen one is followed by position");
        FakeOS os;
        os.ds = {disp(1, "Built-in", 0, 0, 1512, 982), disp(2, "DELL U2720Q", 1512, 0, 2560, 1440),
                 disp(3, "DELL U2720Q", 4072, 0, 2560, 1440)};
        DisplayManager m(os);
        uint64_t now = 1000;
        m.start(now, false);
        m.chooseDisplay(2, true, now);  // the right-hand DELL
        run(m, now, 2000);
        CHECK(os.fullscreenOn(3));
        // Re-plug both in the other OS order: ids change, positions stay.
        os.ds = {disp(1, "Built-in", 0, 0, 1512, 982), disp(11, "DELL U2720Q", 4072, 0, 2560, 1440),
                 disp(12, "DELL U2720Q", 1512, 0, 2560, 1440)};
        m.displaysChanged(now);
        run(m, now, 2000);
        CHECK(os.fullscreenOn(11));
        // And the left-hand one is followed just as well.
        m.chooseDisplay(1, true, now);
        run(m, now, 2000);
        CHECK(os.fullscreenOn(12));
        os.ds = {disp(1, "Built-in", 0, 0, 1512, 982), disp(21, "DELL U2720Q", 1512, 0, 2560, 1440),
                 disp(22, "DELL U2720Q", 4072, 0, 2560, 1440)};
        m.displaysChanged(now);
        run(m, now, 2000);
        CHECK(os.fullscreenOn(21));
    }
    {
        SCENARIO("platform never reaches the exact size: gives up after a few tries (no flicker loop)");
        FakeOS os;
        laptopAndProjector(os);
        os.shrinkOnFullscreen = 40;
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        run(m, now, 30000);
        CHECK(os.enterCalls <= DisplayManager::kMaxAttempts);
    }
    {
        SCENARIO("no displays at all for a moment (e.g. GPU reset) does not crash, then recovers");
        FakeOS os;
        laptopAndProjector(os);
        DisplayManager m(os);
        m.setTarget({"EPSON PJ", 0, 1});
        uint64_t now = 1000;
        m.start(now, true);
        auto saved = os.ds;
        os.ds.clear();
        m.displaysChanged(now);
        run(m, now, 2000);
        os.ds = saved;
        m.displaysChanged(now);
        run(m, now, 2000);
        CHECK(os.fullscreenOn(2));
    }

    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
