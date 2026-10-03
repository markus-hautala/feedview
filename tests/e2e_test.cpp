// End to end: the live-production requirements, checked against the real FeedView.
//  1. The link in FeedView's Remote panel opens the web remote in the browser
//  2. Fullscreen from the web remote comes on top of other windows (also when FeedView was
//     behind another always-on-top window, or minimized) and stays there; the web remote
//     hides FeedView's panels/controls; OS notifications are off while FeedView runs
//  3. The volume is the OS volume; the OS sound output can be chosen
//  4. No PIN by default
//  5. The pointer and controls need the mouse moving for a second, and hide 2 s after it
//     stops (checked with Windows' own cursor state too)
//  6. None is a plain black output: no texts, no controls (checked on the screen's pixels)
//  7. Sources change with a fade: a dissolve with no black in between, a fade in from and
//     out to black, and a cut that still never shows black (screen pixels, sampled)
//
// Usage: e2e_test <FeedView.exe> <feedview-test-sender.exe>
// Starts test senders and FeedView (with its own settings file), drives FeedView through
// the web remote API and simulated mouse/keyboard input, and checks Windows itself. It takes
// over the screen for about a minute - don't touch the mouse meanwhile. The mouse position
// and system volume are put back afterwards. Exit code 77 = skipped (no NDI runtime).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "httplib.h"

#include <windows.h>

#include "os_control.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "check.h"

namespace {

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::wstring wide(const std::string& s) {
    std::wstring w(s.size() * 2 + 1, L'\0');
    w.resize(size_t(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), int(w.size())) - 1));
    return w;
}

std::string readFile(const std::string& path) {
    std::string out;
    if (FILE* f = _wfopen(wide(path).c_str(), L"rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

bool waitFor(const std::function<bool()>& cond, int timeoutMs) {
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    do {
        if (cond()) return true;
        Sleep(100);
    } while (GetTickCount64() < end);
    return cond();
}

struct Process {
    PROCESS_INFORMATION pi{};
    bool start(const std::string& exe, const std::string& args) {
        std::wstring cmd = L"\"" + wide(exe) + L"\" " + wide(args);
        STARTUPINFOW si{};
        si.cb = sizeof si;
        return CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != 0;
    }
    bool running() const { return pi.hProcess && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT; }
    bool waitExit(int ms) const { return pi.hProcess && WaitForSingleObject(pi.hProcess, DWORD(ms)) == WAIT_OBJECT_0; }
    ~Process() {
        if (running()) TerminateProcess(pi.hProcess, 1);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
    }
};

HWND mainWindowOf(DWORD pid) {
    struct Find {
        DWORD pid;
        HWND best = nullptr;
        long area = 0;
    } f{pid};
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto* f = reinterpret_cast<Find*>(p);
            DWORD owner = 0;
            GetWindowThreadProcessId(h, &owner);
            if (owner != f->pid || !IsWindowVisible(h)) return TRUE;
            RECT r{};
            GetWindowRect(h, &r);
            const long a = long(r.right - r.left) * long(r.bottom - r.top);
            if (a > f->area || IsIconic(h)) {
                f->area = a;
                f->best = h;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&f));
    return f.best;
}

HWND windowAt(POINT p) { return GetAncestor(WindowFromPoint(p), GA_ROOT); }

RECT monitorOf(HWND h) {
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcMonitor;
}

// ---- Simulated input (physical pixels; this process is per-monitor DPI aware)

void mouseTo(int x, int y) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = LONG(std::lround((x - vx) * 65535.0 / (vw - 1)));
    in.mi.dy = LONG(std::lround((y - vy) * 65535.0 / (vh - 1)));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof in);
}

// Moves the mouse in small circles around `c` for `ms` (an event every ~16 ms).
void wiggle(POINT c, int ms) {
    const ULONGLONG start = GetTickCount64();
    for (ULONGLONG t = 0; t <= ULONGLONG(ms); t = GetTickCount64() - start) {
        const double a = double(t) / 80.0;
        mouseTo(c.x + int(30 * std::cos(a)), c.y + int(30 * std::sin(a)));
        Sleep(15);
    }
}

void click(POINT p) {
    mouseTo(p.x, p.y);
    Sleep(50);
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}

void key(WORD vk) {
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[0].ki.wScan = in[1].ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

// ---- What's on the screen

bool osCursorVisible() {
    CURSORINFO ci{};
    ci.cbSize = sizeof ci;
    return GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && ci.hCursor;
}

struct Rgb8 {
    int r = 0, g = 0, b = 0;
};

// Copies a screen rectangle (as composed by Windows; the pointer isn't included).
std::vector<uint32_t> capture(RECT r) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    std::vector<uint32_t> px(size_t(w) * size_t(h));
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY);
    GdiFlush();
    std::memcpy(px.data(), bits, px.size() * 4);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return px;
}

Rgb8 pixelAt(POINT p) {
    const auto px = capture(RECT{p.x, p.y, p.x + 1, p.y + 1});
    return {int((px[0] >> 16) & 255), int((px[0] >> 8) & 255), int(px[0] & 255)};
}

// Brightest channel value anywhere in the rectangle: 0 = all black (any text would show).
int maxBrightness(RECT r) {
    int m = 0;
    for (uint32_t p : capture(r)) m = std::max({m, int((p >> 16) & 255), int((p >> 8) & 255), int(p & 255)});
    return m;
}

// Runs `action`, then samples the colour at `p` for `ms`.
std::vector<Rgb8> sampleAfter(POINT p, int ms, const std::function<void()>& action) {
    std::vector<Rgb8> out;
    action();
    const ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        out.push_back(pixelAt(p));
        Sleep(8);
    }
    return out;
}

// Centre on the screen of a control FeedView reports in output.ui (it does when started with
// FEEDVIEW_TEST_UI), from the state JSON; {-1,-1} if it isn't drawn right now.
RECT controlRect(HWND fv, const std::string& state, const std::string& name) {
    const size_t at = state.find("\"" + name + "\":[");
    if (at == std::string::npos) return {-1, -1, -1, -1};
    int x = 0, y = 0, w = 0, h = 0;
    if (std::sscanf(state.c_str() + at + name.size() + 4, "%d,%d,%d,%d", &x, &y, &w, &h) != 4) return {-1, -1, -1, -1};
    POINT a{x, y};
    ClientToScreen(fv, &a);
    return {a.x, a.y, a.x + w, a.y + h};
}

POINT controlCenter(HWND fv, const std::string& state, const std::string& name) {
    const RECT r = controlRect(fv, state, name);
    if (r.left < 0) return {-1, -1};
    return {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
}

// Pure white pixels in a screen rectangle (a QR code has many; a 75 % colour-bar picture none).
int countWhite(RECT r) {
    if (r.left < 0 || r.right <= r.left || r.bottom <= r.top) return -1;
    int n = 0;
    for (uint32_t p : capture(r))
        if (((p >> 16) & 255) >= 250 && ((p >> 8) & 255) >= 250 && (p & 255) >= 250) ++n;
    return n;
}

// ---- "Another program": a window owned by another thread, with its own message loop.

struct OtherApp {
    std::thread t;
    HWND hwnd = nullptr;
    DWORD threadId = 0;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    void start(RECT r, DWORD exStyle) {
        t = std::thread([this, r, exStyle] {
            threadId = GetCurrentThreadId();
            WNDCLASSW wc{};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"FeedViewE2eOtherApp";
            wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));
            RegisterClassW(&wc);
            hwnd = CreateWindowExW(exStyle, wc.lpszClassName, L"Another program", WS_POPUP, r.left, r.top,
                                   r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            SetEvent(ready);
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) DispatchMessageW(&m);
            DestroyWindow(hwnd);
        });
        WaitForSingleObject(ready, 5000);
    }
    void stop() {
        if (!t.joinable()) return;
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        t.join();
    }
    ~OtherApp() {
        stop();
        CloseHandle(ready);
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Usage: e2e_test <FeedView.exe> <feedview-test-sender.exe>\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const DWORD me = GetCurrentProcessId();

    // FeedView gets its own settings file (fresh: the defaults are under test) and records
    // the links it opens instead of starting a browser.
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    char tmpUtf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, tmpUtf8, sizeof tmpUtf8, nullptr, nullptr);
    const std::string dir = std::string(tmpUtf8) + "feedview-e2e-" + std::to_string(me);
    CreateDirectoryW(wide(dir).c_str(), nullptr);
    const std::string settingsPath = dir + "\\settings.ini", urlLog = dir + "\\opened-urls.txt";
    DeleteFileW(wide(settingsPath).c_str());
    DeleteFileW(wide(urlLog).c_str());
    SetEnvironmentVariableW(L"FEEDVIEW_TEST_OPEN_URL_LOG", wide(urlLog).c_str());
    SetEnvironmentVariableW(L"FEEDVIEW_TEST_UI", L"1");  // FeedView tells where its controls are, to click them

    POINT cursor0{};
    GetCursorPos(&cursor0);
    os::SystemAudio sys;
    const int volume0 = sys.volume();
    const bool permitted = os::NotificationSilencer().permitted();
    const std::string policy0 = os::notificationPolicyValueForTests();
    struct Restore {
        POINT cursor;
        int volume;
        os::SystemAudio& sys;
        ~Restore() {
            SetCursorPos(cursor.x, cursor.y);
            sys.update(0, true);
            if (volume >= 0 && sys.volume() != volume) sys.setVolume(volume);
        }
    } restore{cursor0, volume0, sys};

    // ---- Start the test source and FeedView
    const std::string sourceName = "FeedView E2E " + std::to_string(me);
    Process sender;
    if (!sender.start(argv[2], "--name \"" + sourceName + "\" --size 1280x720 --fps 30 --seconds 300")) {
        std::printf("SKIPPED: couldn't start the test sender\n");
        return 77;
    }
    Sleep(1500);
    if (!sender.running()) {
        std::printf("SKIPPED: the test sender stopped (is the NDI runtime installed?)\n");
        return 77;
    }
    // Two plain colours, to see fades between sources on the screen's pixels.
    const std::string redName = "FeedView E2E Red " + std::to_string(me), blueName = "FeedView E2E Blue " + std::to_string(me);
    Process red, blue;
    CHECK(red.start(argv[2], "--name \"" + redName + "\" --solid FF0000 --size 640x360 --fps 30 --channels 0 --seconds 300"));
    CHECK(blue.start(argv[2], "--name \"" + blueName + "\" --solid 0000FF --size 640x360 --fps 30 --channels 0 --seconds 300"));
    const int basePort = 20000 + int(me % 1000) * 10;
    Process app;
    CHECK(app.start(argv[1], "--settings \"" + settingsPath + "\" --remote-port " + std::to_string(basePort) +
                                 " --extra-ips 127.0.0.1"));

    int port = 0;
    waitFor(
        [&] {
            for (int p = basePort; p < basePort + 10 && !port; ++p) {
                httplib::Client c("127.0.0.1", p);
                c.set_connection_timeout(0, 200000);
                auto r = c.Get("/api/ping");
                if (r && r->status == 200 && contains(r->body, "\"app\":\"FeedView\"")) port = p;
            }
            return port != 0;
        },
        20000);
    HWND fv = nullptr;
    waitFor([&] { return (fv = mainWindowOf(app.pi.dwProcessId)) != nullptr; }, 10000);
    if (!port || !fv) {
        std::printf("  FAIL: FeedView didn't start (port %d, window %p)\n", port, static_cast<void*>(fv));
        return 1;
    }
    std::printf("FeedView is up: web remote on port %d\n", port);

    httplib::Client cli("127.0.0.1", port);
    cli.set_read_timeout(10, 0);
    const httplib::Headers cmdHeader = {{"X-FeedView-Pin", ""}};  // commands need the header, any value without a PIN
    auto state = [&]() -> std::string {
        auto r = cli.Get("/api/state");
        return r && r->status == 200 ? r->body : std::string();
    };
    auto post = [&](const std::string& action, const httplib::Params& params) -> int {
        auto r = cli.Post("/api/" + action, cmdHeader, params);
        return r ? r->status : 0;
    };
    auto controlsVisible = [&] { return contains(state(), "\"controls\":{\"visible\":true"); };
    // Longest time between two FeedView frames in the last ~5 s: a stall freezes the picture.
    auto frameGap = [&]() -> int {
        const std::string s = state();
        const size_t at = s.find("\"frameGapMs\":");
        return at == std::string::npos ? -1 : std::atoi(s.c_str() + at + 13);
    };

    // ---------------------------------------------------------------- 4
    {
        SCENARIO("[REQ-09] no PIN by default: the remote works without one");
        auto p = cli.Get("/api/ping");
        CHECK(p && contains(p->body, "\"pinRequired\":false"));
        CHECK(!state().empty());
    }

    // ---------------------------------------------------------------- 6 (first start)
    auto clientRect = [&] {
        RECT c{};
        GetClientRect(fv, &c);
        POINT a{c.left, c.top}, b{c.right, c.bottom};
        ClientToScreen(fv, &a);
        ClientToScreen(fv, &b);
        return RECT{a.x + 2, a.y + 2, b.x - 2, b.y - 2};  // inside Windows 11's 1-pixel window border
    };
    {
        SCENARIO("[REQ-14][REQ-15] a fresh start has None: FeedView's window is black, without any text");
        CHECK(contains(state(), "\"source\":{\"name\":\"\""));
        Sleep(1500);  // the searching/Remote hints of earlier versions would be up by now
        const int m = maxBrightness(clientRect());
        std::printf("  brightest pixel in the window: %d\n", m);
        CHECK(m <= 8);
        CHECK(!controlsVisible());
        SCENARIO("[REQ-14] None stays black with the info line switched on and while FeedView has a message to show");
        CHECK(post("settings", {{"show_info", "1"}}) == 200);
        CHECK(post("display", {{"number", "1"}, {"fullscreen", "0"}}) == 200);  // "Moved to ..." message
        Sleep(800);
        const int m2 = maxBrightness(clientRect());
        std::printf("  brightest pixel in the window: %d\n", m2);
        CHECK(m2 <= 8);
        CHECK(contains(state(), "Moved to"));  // the message exists (in the remote's events), just not on screen
        CHECK(post("settings", {{"show_info", "0"}}) == 200);
    }

    // ---- Find the test sources; put the bars on (through the web remote, as an operator would)
    auto fullName = [&](const std::string& part) {
        std::string found;
        waitFor(
            [&] {
                const std::string s = state();
                const size_t at = s.find("(" + part + ")");
                if (at == std::string::npos) return false;
                const size_t a = s.rfind('"', at) + 1, b = s.find('"', at);
                found = s.substr(a, b - a);
                return true;
            },
            30000);
        return found;
    };
    const std::string source = fullName(sourceName), redSource = fullName(redName), blueSource = fullName(blueName);
    CHECK(!source.empty() && !redSource.empty() && !blueSource.empty());
    CHECK(post("source", {{"name", source}}) == 200);
    CHECK(waitFor([&] { return contains(state(), "\"hasPicture\":true"); }, 20000));

    RECT wr{};
    GetWindowRect(fv, &wr);
    const POINT inPicture{(wr.left + wr.right) / 2, wr.top + (wr.bottom - wr.top) * 3 / 5};  // below the bar

    // ---------------------------------------------------------------- 5
    // Moves the mouse for `ms` while watching the state; returns when (ms after the start)
    // the controls first showed, or -1.
    auto showsAfter = [&](int ms) {
        std::thread mover([&] { wiggle(inPicture, ms); });
        const ULONGLONG start = GetTickCount64();
        long long shown = -1;
        while (GetTickCount64() - start < ULONGLONG(ms) + 400 && shown < 0) {
            if (controlsVisible()) shown = (long long)(GetTickCount64() - start);
            Sleep(40);
        }
        mover.join();
        return shown;
    };
    auto cursorShown = [&] { return contains(state(), "\"cursor\":true"); };
    {
        SCENARIO("[REQ-10][REQ-12] with a picture, FeedView's pointer and controls are hidden");
        mouseTo(inPicture.x, inPicture.y);
        CHECK(waitFor([&] { return !controlsVisible() && !cursorShown(); }, 6000));
        Sleep(100);
        CHECK(!osCursorVisible());  // Windows agrees: no pointer over FeedView
        SCENARIO("[REQ-10][REQ-13] moving the mouse over the picture for 0.5 s doesn't show them");
        CHECK(showsAfter(500) < 0);
        CHECK(!osCursorVisible());
        Sleep(600);  // a pause: the next movement counts from zero
        SCENARIO("[REQ-10][REQ-13] moving it for over a second does, after about one second");
        const long long t = showsAfter(1600);
        std::printf("  pointer and controls showed after %lld ms of movement\n", t);
        CHECK(t >= 950 && t <= 1500);
        CHECK(cursorShown());
        CHECK(osCursorVisible());
        SCENARIO("[REQ-12] they hide again 2 s after the mouse stops");
        Sleep(1600);
        CHECK(controlsVisible() && cursorShown());
        const ULONGLONG stillSince = GetTickCount64() - 1600;
        CHECK(waitFor([&] { return !controlsVisible() && !cursorShown(); }, 1000));
        const ULONGLONG hidAfter = GetTickCount64() - stillSince;
        std::printf("  hidden %llu ms after the mouse stopped\n", hidAfter);
        CHECK(hidAfter >= 1900 && hidAfter <= 2600);
        Sleep(100);
        CHECK(!osCursorVisible());
        SCENARIO("the picture never froze meanwhile (no frame took over 100 ms)");
        const int gap = frameGap();
        std::printf("  longest frame gap: %d ms\n", gap);
        CHECK(gap >= 0 && gap < 100);
    }

    // ---------------------------------------------------------------- 2: panels
    {
        SCENARIO("[REQ-04][REQ-10] a click on the picture shows nothing; R opens the Remote panel (QR code)");
        click(inPicture);  // also gives FeedView the keyboard
        Sleep(400);
        CHECK(!controlsVisible());
        key('R');
        CHECK(waitFor([&] { return contains(state(), "\"panel\":\"remote\""); }, 3000));
        CHECK(controlsVisible());
        SCENARIO("[REQ-04] the web remote hides the panel and the controls");
        CHECK(post("controls", {{"on", "0"}}) == 200);
        CHECK(waitFor([&] { return contains(state(), "\"controls\":{\"visible\":false,\"panel\":\"\""); }, 3000));
    }

    // ---------------------------------------------------------------- 1
    {
        SCENARIO("[REQ-01] clicking the address in FeedView's Remote panel opens it in the browser");
        // As a person would: move the mouse until the controls show, click Remote, click the link.
        CHECK(showsAfter(1300) >= 0);
        const POINT remoteButton = controlCenter(fv, state(), "remote_button");
        CHECK(remoteButton.x >= 0);
        click(remoteButton);
        CHECK(waitFor([&] { return contains(state(), "\"panel\":\"remote\""); }, 3000));
        Sleep(300);
        const POINT link = controlCenter(fv, state(), "remote_link");
        CHECK(link.x >= 0);
        std::printf("  clicking the link at %ld,%ld\n", link.x, link.y);
        click(link);
        std::string opened;
        CHECK(waitFor([&] { return !(opened = readFile(urlLog)).empty(); }, 3000));
        std::printf("  opened: %s", opened.c_str());
        CHECK(contains(opened, "http://") && contains(opened, ":" + std::to_string(port) + "/\n"));
        CHECK(!contains(opened, "#pin="));  // no PIN needed
        CHECK(waitFor([&] { return contains(state(), "\"panel\":\"\""); }, 3000));  // panel closed
        DeleteFileW(wide(urlLog).c_str());
    }
    {
        SCENARIO("[REQ-01] the Remote panel's link opens the web remote in the browser (Enter)");
        key('R');
        CHECK(waitFor([&] { return contains(state(), "\"panel\":\"remote\""); }, 3000));
        key(VK_RETURN);
        std::string opened;
        CHECK(waitFor([&] { return !(opened = readFile(urlLog)).empty(); }, 3000));
        std::printf("  opened: %s", opened.c_str());
        CHECK(contains(opened, "http://") && contains(opened, ":" + std::to_string(port) + "/\n"));
        CHECK(!contains(opened, "#pin="));  // no PIN needed
        CHECK(waitFor([&] { return contains(state(), "\"panel\":\"\""); }, 3000));  // panel closed
        SCENARIO("[REQ-01][REQ-02] FeedView steps back so the browser isn't hidden behind it");
        CHECK(waitFor([&] { return contains(state(), "\"onTop\":false"); }, 2000));
        CHECK(!os::isTopmost(fv));
    }

    // ---------------------------------------------------------------- 3
    {
        SCENARIO("[REQ-07] the remote's volume is the OS volume");
        const std::string s = state();
        if (!sys.available()) {
            CHECK(contains(s, "\"system\":false"));
            SKIP("no audio output on this computer");
        } else {
            CHECK(contains(s, "\"system\":true"));
            CHECK(contains(s, "\"output\":\"" + sys.defaultOutput() + "\""));
            const int target = volume0 < 100 ? volume0 + 1 : volume0 - 1;
            CHECK(post("volume", {{"value", std::to_string(target)}}) == 200);
            CHECK(os::SystemAudio().volume() == target);
            CHECK(contains(state(), "\"volume\":" + std::to_string(target) + ","));
            CHECK(post("volume", {{"value", std::to_string(volume0)}}) == 200);
            CHECK(os::SystemAudio().volume() == volume0);
            SCENARIO("[REQ-07] mute is the OS mute (sent unchanged: nothing audible changes)");
            const bool muted = os::SystemAudio().muted();
            CHECK(post("mute", {{"on", muted ? "1" : "0"}}) == 200);
            CHECK(os::SystemAudio().muted() == muted);
            SCENARIO("[REQ-08] the OS sound output can be chosen; unknown ones are refused");
            CHECK(post("audio-output", {{"id", sys.defaultOutput()}}) == 200);
            CHECK(os::SystemAudio().defaultOutput() == sys.defaultOutput());
            CHECK(post("audio-output", {{"id", "{no-such-device}"}}) == 404);
            SCENARIO("[REQ-07] audio changes don't freeze the picture (no frame took over 100 ms)");
            const int gap = frameGap();
            std::printf("  longest frame gap: %d ms\n", gap);
            CHECK(gap >= 0 && gap < 100);
        }
    }

    // ---------------------------------------------------------------- 2: on top
    const RECT mon = monitorOf(fv);
    const POINT center{(mon.left + mon.right) / 2, (mon.top + mon.bottom) / 2};
    const POINT taskbar{(mon.left + mon.right) / 2, mon.bottom - 3};  // where the taskbar usually is
    {
        SCENARIO("[REQ-02][REQ-03] fullscreen from the web remote comes above another program's always-on-top window");
        OtherApp cover;
        cover.start(mon, WS_EX_TOPMOST);
        Sleep(500);
        CHECK(os::coveredByOtherWindow(fv));  // FeedView really is behind it
        const bool coverHasFocus = GetForegroundWindow() == cover.hwnd;
        CHECK(post("fullscreen", {{"on", "1"}}) == 200);
        Sleep(1500);
        const std::string s = state();
        CHECK(contains(s, "\"fullscreen\":true"));
        CHECK(contains(s, "\"onTop\":true"));
        CHECK(os::isTopmost(fv));
        CHECK(windowAt(center) == fv);
        CHECK(windowAt(taskbar) == fv);
        CHECK(!os::coveredByOtherWindow(fv));
        if (coverHasFocus) CHECK(GetForegroundWindow() == cover.hwnd);  // the keyboard isn't taken
        SCENARIO("[REQ-04] the output is clean: no controls or panels over it");
        CHECK(contains(s, "\"controls\":{\"visible\":false"));

        SCENARIO("[REQ-02] when the other program pushes itself on top again, FeedView comes back within a second");
        SetWindowPos(cover.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        CHECK(os::coveredByOtherWindow(fv));  // checked at once: FeedView looks twice a second
        Sleep(1500);
        CHECK(windowAt(center) == fv);
        cover.stop();
        Sleep(1200);
        CHECK(windowAt(center) == fv);
        CHECK(windowAt(taskbar) == fv);  // the taskbar doesn't come back over the output
    }
    {
        SCENARIO("[REQ-03] fullscreen from the web remote also brings back a minimized FeedView");
        CHECK(post("fullscreen", {{"on", "0"}}) == 200);
        CHECK(waitFor([&] { return contains(state(), "\"fullscreen\":false"); }, 3000));
        ShowWindow(fv, SW_MINIMIZE);
        CHECK(waitFor([&] { return IsIconic(fv) != 0; }, 3000));
        Sleep(2000);
        CHECK(IsIconic(fv));  // minimized by someone at the computer: FeedView doesn't fight it
        CHECK(post("fullscreen", {{"on", "1"}}) == 200);
        Sleep(1500);
        CHECK(!IsIconic(fv));
        CHECK(contains(state(), "\"fullscreen\":true"));
        CHECK(windowAt(center) == fv);
    }
    {
        SCENARIO("[REQ-02][REQ-06] 'Always on top' can be switched off and on from the web remote");
        CHECK(post("settings", {{"always_on_top", "0"}}) == 200);
        Sleep(300);
        CHECK(!os::isTopmost(fv));
        CHECK(contains(state(), "\"alwaysOnTop\":false"));
        CHECK(post("settings", {{"always_on_top", "1"}}) == 200);
        Sleep(300);
        CHECK(os::isTopmost(fv));
        CHECK(contains(state(), "\"onTop\":true"));
        std::printf("  (longest frame gap around the fullscreen changes: %d ms)\n", frameGap());
    }
    {
        SCENARIO("[REQ-04] the Remote panel (QR code) opened on the fullscreen output is hidden from the remote");
        click(center);  // gives FeedView the keyboard; a click on the picture shows nothing
        Sleep(300);
        key('R');
        CHECK(waitFor([&] { return contains(state(), "\"panel\":\"remote\""); }, 3000));
        Sleep(400);
        const std::string s = state();
        CHECK(contains(s, "\"fullscreen\":true"));
        const RECT qr = controlRect(fv, s, "remote_qr");
        const int before = countWhite(qr);
        std::printf("  QR code on the output: %d white pixels\n", before);
        CHECK(before > 200);
        CHECK(post("controls", {{"on", "0"}}) == 200);
        CHECK(waitFor([&] { return contains(state(), "\"controls\":{\"visible\":false,\"panel\":\"\""); }, 3000));
        Sleep(300);
        const int after = countWhite(qr);
        std::printf("  after Hide from the remote: %d white pixels there\n", after);
        CHECK(after == 0);
        SCENARIO("[REQ-04][REQ-03] Fullscreen from the remote also clears a panel left open on the output");
        key('R');
        CHECK(waitFor([&] { return contains(state(), "\"panel\":\"remote\""); }, 3000));
        Sleep(300);
        CHECK(countWhite(qr) > 200);
        CHECK(post("fullscreen", {{"on", "1"}}) == 200);
        CHECK(waitFor([&] { return contains(state(), "\"controls\":{\"visible\":false,\"panel\":\"\""); }, 3000));
        Sleep(300);
        CHECK(countWhite(qr) == 0);
        CHECK(contains(state(), "\"fullscreen\":true"));
    }

    // ---------------------------------------------------------------- 6 + 7 (fullscreen)
    auto isRed = [](Rgb8 c) { return c.r > 200 && c.g < 60 && c.b < 60; };
    auto isBlue = [](Rgb8 c) { return c.b > 200 && c.r < 60 && c.g < 60; };
    auto isBlack = [](Rgb8 c) { return c.r < 12 && c.g < 12 && c.b < 12; };
    auto count = [](const std::vector<Rgb8>& v, const std::function<bool(Rgb8)>& pred) {
        return int(std::count_if(v.begin(), v.end(), pred));
    };
    auto inBetween = [](Rgb8 c) { return c.r >= 40 && c.r <= 200 && c.b < 40; };  // red, partly faded
    auto mixed = [](Rgb8 c) { return c.r > 40 && c.b > 40; };                     // red and blue at once
    auto dark = [](Rgb8 c) { return c.r + c.b < 180; };                           // a dip towards black
    auto describe = [](const std::vector<Rgb8>& v) {
        std::printf("  %zu samples:", v.size());
        for (size_t i = 0; i < v.size(); i += std::max<size_t>(1, v.size() / 12))
            std::printf(" (%d,%d,%d)", v[i].r, v[i].g, v[i].b);
        std::printf("\n");
    };
    {
        SCENARIO("[REQ-14][REQ-15] None from the web remote: the fullscreen output is plain black, no texts or controls");
        CHECK(post("settings", {{"show_info", "1"}}) == 200);  // even with the info line switched on
        CHECK(post("source", {{"name", ""}, {"fade_ms", "0"}}) == 200);
        CHECK(waitFor([&] { return contains(state(), "\"picture\":false"); }, 3000));
        Sleep(800);
        const int m = maxBrightness(mon);
        std::printf("  brightest pixel on the screen: %d\n", m);
        CHECK(m <= 8);
        CHECK(!controlsVisible());
        SCENARIO("[REQ-14][REQ-10] a bump of the mouse still shows nothing");
        wiggle(center, 400);
        Sleep(200);
        CHECK(maxBrightness(mon) <= 8);
        CHECK(!controlsVisible() && !cursorShown());
        CHECK(post("settings", {{"show_info", "0"}}) == 200);
        SCENARIO("[REQ-18][REQ-06] the fade time is set from the web remote");
        CHECK(post("settings", {{"fade_ms", "1000"}}) == 200);
        CHECK(contains(state(), "\"fadeMs\":1000"));
    }
    {
        SCENARIO("[REQ-18][REQ-14] taking a source from None: while it connects and fades in, nothing else appears (no text, no controls)");
        // The whole screen, several times: only black or the red picture fading in is allowed.
        CHECK(post("source", {{"name", redSource}}) == 200);
        int foreign = 0, captures = 0;
        for (const ULONGLONG end = GetTickCount64() + 1500; GetTickCount64() < end; Sleep(100)) {
            for (uint32_t p : capture(mon))
                if (((p >> 8) & 255) > 40 || (p & 255) > 40) ++foreign;
            ++captures;
        }
        std::printf("  %d screen captures, %d pixels that are neither black nor red\n", captures, foreign);
        CHECK(captures >= 5);
        CHECK(foreign == 0);
        CHECK(post("source", {{"name", ""}, {"fade_ms", "0"}}) == 200);  // back to None for the next one
        CHECK(waitFor([&] { return contains(state(), "\"picture\":false"); }, 3000));
        Sleep(500);
    }
    {
        SCENARIO("[REQ-18] from None, the new source fades in from black (1 s)");
        const auto s = sampleAfter(center, 4000, [&] { post("source", {{"name", redSource}}); });
        describe(s);
        CHECK(!s.empty() && isBlack(s.front()) && isRed(s.back()));
        CHECK(count(s, inBetween) >= 3);
    }
    {
        SCENARIO("[REQ-18] red to blue: red stays until blue is up, then they dissolve - never black in between");
        const auto s = sampleAfter(center, 4000, [&] { post("source", {{"name", blueSource}}); });
        describe(s);
        CHECK(!s.empty() && isRed(s.front()) && isBlue(s.back()));
        CHECK(count(s, dark) == 0);
        CHECK(count(s, mixed) >= 3);
    }
    {
        SCENARIO("[REQ-18] a cut (fade_ms=0 for one take): no dissolve, and no black while the new source connects");
        const auto s = sampleAfter(center, 3000, [&] { post("source", {{"name", redSource}, {"fade_ms", "0"}}); });
        describe(s);
        CHECK(!s.empty() && isBlue(s.front()) && isRed(s.back()));
        CHECK(count(s, dark) == 0);
        CHECK(count(s, mixed) == 0);
        CHECK(contains(state(), "\"fadeMs\":1000"));  // a one-off: the setting is unchanged
    }
    {
        SCENARIO("[REQ-18][REQ-14] taking None fades the picture out to black");
        const auto s = sampleAfter(center, 2500, [&] { post("source", {{"name", ""}}); });
        describe(s);
        CHECK(!s.empty() && isRed(s.front()) && isBlack(s.back()));
        CHECK(count(s, inBetween) >= 3);
        Sleep(300);
        CHECK(maxBrightness(mon) <= 8);
        SCENARIO("[REQ-18] the picture never froze during the takes (no frame over 100 ms)");
        const int gap = frameGap();
        std::printf("  longest frame gap: %d ms\n", gap);
        CHECK(gap >= 0 && gap < 100);
    }
    CHECK(post("fullscreen", {{"on", "0"}}) == 200);

    // ---------------------------------------------------------------- 2: notifications
    {
        SCENARIO("[REQ-05] OS notifications are off while FeedView runs");
        const std::string s = state();
        if (permitted) {
            CHECK(os::notificationPolicyValueForTests() == "1");
            CHECK(contains(s, "\"silenced\":true"));
        } else {
            CHECK(contains(s, "\"permitted\":false"));
            SKIP("FeedView hasn't been given the one-time permission (Settings > Allow)");
        }
    }

    // ---------------------------------------------------------------- shutdown
    {
        SCENARIO("[REQ-05][REQ-09] FeedView closes cleanly; notifications are back as they were; settings were saved");
        PostMessageW(fv, WM_CLOSE, 0, 0);
        CHECK(app.waitExit(15000));
        if (permitted) CHECK(os::notificationPolicyValueForTests() == policy0);
        const std::string saved = readFile(settingsPath);
        CHECK(contains(saved, "settings_version=2"));
        CHECK(contains(saved, "remote_require_pin=0"));
        CHECK(contains(saved, "silenced_notifications=0"));
    }

    DeleteFileW(wide(settingsPath).c_str());
    DeleteFileW(wide(urlLog).c_str());
    RemoveDirectoryW(wide(dir).c_str());
    return finish();
}
