// Checks os_control against the real OS of this computer (requirements 2 and 3):
//  * system audio: outputs, default output, volume and mute round trips
//  * OS notifications switched off and back (needs the one-time permission; skipped without)
//  * a window kept above another program's always-on-top window, without taking focus
// Everything it changes is put back at once (the volume moves by 1 step and back).
// Optional: FEEDVIEW_TEST_SWITCH_OUTPUT=1 also switches to another output and back.

#include "os_control.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#endif

#include "check.h"

#if defined(_WIN32)

static void pump() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static void waitMs(int ms) {
    const ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        pump();
        Sleep(10);
    }
}

// For failures: what lies above `h` (and overlaps it) in the z-order.
static void printWindowsAbove(HWND h) {
    RECT me{};
    GetWindowRect(h, &me);
    for (HWND w = GetWindow(h, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV)) {
        if (!IsWindowVisible(w)) continue;
        RECT r{}, x{};
        GetWindowRect(w, &r);
        if (!IntersectRect(&x, &r, &me)) continue;
        wchar_t cls[128] = {}, title[128] = {};
        GetClassNameW(w, cls, 128);
        GetWindowTextW(w, title, 128);
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        std::printf("    above: class=%ls title=\"%ls\" pid=%lu ex=0x%lx rect=%ld,%ld-%ld,%ld\n", cls, title, pid,
                    GetWindowLongW(w, GWL_EXSTYLE), r.left, r.top, r.right, r.bottom);
    }
}

static HWND makeWindow(const wchar_t* title, DWORD exStyle, RECT r) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"FeedViewOsControlTest";
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH));
        RegisterClassW(&wc);
        registered = true;
    }
    return CreateWindowExW(exStyle | WS_EX_TOOLWINDOW, L"FeedViewOsControlTest", title, WS_POPUP, r.left, r.top,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// "Another program": a window owned by another thread (with its own message loop).
struct OtherApp {
    std::thread t;
    std::atomic<HWND> hwnd{nullptr};
    DWORD threadId = 0;
    std::atomic<bool> ready{false};

    void start(DWORD exStyle, RECT r) {
        t = std::thread([this, exStyle, r] {
            threadId = GetCurrentThreadId();
            HWND h = makeWindow(L"Other app", exStyle, r);
            if (exStyle & WS_EX_LAYERED) SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
            ShowWindow(h, SW_SHOWNOACTIVATE);
            hwnd = h;
            ready = true;
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) DispatchMessageW(&m);
            DestroyWindow(h);
        });
        while (!ready) Sleep(5);
    }
    void stop() {
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        t.join();
    }
};

static void windowTests() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const RECT area{60, 60, 420, 300};

    SCENARIO("[REQ-02][REQ-03] FeedView's window goes above another program's always-on-top window, without taking focus");
    HWND output = makeWindow(L"Output", 0, area);
    ShowWindow(output, SW_SHOWNOACTIVATE);
    OtherApp other;
    other.start(WS_EX_TOPMOST, area);
    waitMs(200);
    CHECK(os::coveredByOtherWindow(output));
    CHECK(!os::isTopmost(output));
    os::raiseWindow(output, true);
    waitMs(100);
    CHECK(os::isTopmost(output));
    CHECK(!os::coveredByOtherWindow(output));
    if (os::coveredByOtherWindow(output)) printWindowsAbove(output);
    CHECK(GetForegroundWindow() != output);

    SCENARIO("[REQ-02] when the other program pushes itself on top again, keepOnTop puts FeedView back");
    SetWindowPos(other.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    waitMs(100);
    CHECK(os::coveredByOtherWindow(output));
    CHECK(os::keepOnTop(output));
    waitMs(100);
    CHECK(!os::coveredByOtherWindow(output));
    CHECK(!os::keepOnTop(output));  // nothing to do now
    other.stop();

    SCENARIO("[REQ-02] click-through overlays (invisible) don't count as covering");
    OtherApp overlay;
    overlay.start(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT, area);
    waitMs(100);
    CHECK(!os::coveredByOtherWindow(output));
    overlay.stop();

    SCENARIO("[REQ-03] a minimized output is brought back without taking focus");
    ShowWindow(output, SW_MINIMIZE);
    waitMs(200);
    CHECK(IsIconic(output));
    os::raiseWindow(output, true);
    waitMs(200);
    CHECK(!IsIconic(output));
    CHECK(GetForegroundWindow() != output);
    DestroyWindow(output);
    pump();
}

#endif

int main() {
    // ---------------------------------------------------------------- 3. system audio
    {
        os::SystemAudio sys;
#if defined(_WIN32)
        SCENARIO("[REQ-07][REQ-08] the system volume and the OS audio outputs are available");
        if (!sys.available()) {
            SKIP("no audio output on this computer");
        } else {
            CHECK(!sys.outputs().empty());
            int defaults = 0;
            for (const auto& o : sys.outputs()) {
                CHECK(!o.id.empty() && !o.name.empty());
                if (o.isDefault) {
                    ++defaults;
                    CHECK(o.id == sys.defaultOutput());
                    CHECK(o.name == sys.defaultOutputName());
                }
            }
            CHECK(defaults == 1);
            std::printf("  default output: %s, volume %d%%%s\n", sys.defaultOutputName().c_str(), sys.volume(),
                        sys.muted() ? ", muted" : "");

            SCENARIO("[REQ-07] keeping up with the OS is cheap enough for the UI thread (no video hitches)");
            LARGE_INTEGER f, a, b;
            QueryPerformanceFrequency(&f);
            double worst = 0;
            for (int i = 0; i < 20; ++i) {
                QueryPerformanceCounter(&a);
                sys.update(uint64_t(i) * 300);  // as FeedView's frame loop calls it
                QueryPerformanceCounter(&b);
                const double ms = double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart);
                if (ms > worst) worst = ms;
            }
            std::printf("  slowest update: %.2f ms\n", worst);
            CHECK(worst < 4.0);

            SCENARIO("[REQ-07] setting the volume changes the OS volume (seen by another reader), then restored");
            const int v0 = sys.volume();
            CHECK(v0 >= 0 && v0 <= 100);
            const int v1 = v0 < 100 ? v0 + 1 : v0 - 1;
            os::SystemAudio watcher;  // like FeedView seeing a change made in Windows
            CHECK(sys.setVolume(v1));
            CHECK(sys.volume() == v1);
            {
                os::SystemAudio reader;
                CHECK(reader.volume() == v1);
            }
            SCENARIO("[REQ-07] a volume change made elsewhere reaches FeedView (Windows notifies it)");
            bool seen = false;
            for (int i = 0; i < 50 && !seen; ++i) {
                Sleep(20);
                watcher.update(GetTickCount64());
                seen = watcher.volume() == v1;
            }
            CHECK(seen);
            CHECK(sys.setVolume(v0));
            {
                os::SystemAudio reader;
                CHECK(reader.volume() == v0);
            }

            SCENARIO("[REQ-07] mute is the OS mute (set to its current value: nothing audible changes)");
            const bool m0 = sys.muted();
            CHECK(sys.setMuted(m0));
            CHECK(sys.muted() == m0);

            SCENARIO("[REQ-08] the default output can be chosen; unknown ids are refused");
            const std::string d0 = sys.defaultOutput();
            CHECK(sys.setDefaultOutput(d0));
            CHECK(sys.defaultOutput() == d0);
            CHECK(!sys.setDefaultOutput("{no-such-device}"));
            CHECK(sys.defaultOutput() == d0);
            char sw[8] = {};
            GetEnvironmentVariableA("FEEDVIEW_TEST_SWITCH_OUTPUT", sw, sizeof sw);
            if (sw[0] == '1' && sys.outputs().size() > 1) {
                SCENARIO("[REQ-08] switching to another output and back");
                std::string other;
                for (const auto& o : sys.outputs())
                    if (o.id != d0) other = o.id;
                CHECK(sys.setDefaultOutput(other));
                CHECK(os::SystemAudio().defaultOutput() == other);
                CHECK(sys.setDefaultOutput(d0));
                CHECK(os::SystemAudio().defaultOutput() == d0);
            }
        }
#else
        SCENARIO("[REQ-07] not available on this OS (FeedView uses its own volume)");
        CHECK(!sys.available());
        CHECK(!sys.setVolume(50));
#endif
    }

    // ---------------------------------------------------------------- 2. notifications
    {
        os::NotificationSilencer n;
#if defined(_WIN32)
        SCENARIO("[REQ-05] OS notifications are switched off and back on");
        CHECK(n.supported());
        if (!n.permitted()) {
            SKIP("FeedView hasn't been given the one-time permission on this computer (Settings > Allow)");
        } else {
            const std::string before = os::notificationPolicyValueForTests();
            CHECK(n.silence(true, true));
            CHECK(os::notificationPolicyValueForTests() == "1");
            CHECK(n.isSilenced());
            CHECK(n.silence(false, true));
            CHECK(os::notificationPolicyValueForTests().empty());
            CHECK(!n.isSilenced());
            if (before == "1") n.silence(true, true);  // e.g. a running FeedView had them off
            CHECK(os::notificationPolicyValueForTests() == before);
        }
#else
        SCENARIO("[REQ-05] notifications: not available on this OS");
        CHECK(!n.supported());
        CHECK(!n.silence(true));
#endif
    }

    // ---------------------------------------------------------------- 2. staying on top
#if defined(_WIN32)
    windowTests();
#endif

    return finish();
}
