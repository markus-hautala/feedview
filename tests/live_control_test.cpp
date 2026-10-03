// Live-production rules as pure logic, one group per requirement:
//  pointer:  the pointer and controls show after a second of moving the mouse and hide 2 s
//            after the last input
//  panels:   panels (e.g. the QR code) never stay on the output; the web remote can hide them
//  fade:     a take dissolves from the old picture to the new one (or to/from black)
//  links:    FeedView's remote links open the page signed in
//  PIN:      the PIN is off by default (also for settings written by FeedView 1.0)
//  state:    what the web remote (and the Companion module) gets
// The same requirements are checked against the real OS and app in os_control_test.cpp and
// e2e_test.cpp, and through the Companion module in companion/feedview/test.

#include "overlay_activity.h"
#include "remote_view.h"
#include "settings.h"
#include "take_fade.h"

#include "check.h"

#include <cstdio>
#include <string>

static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// Moves the mouse continuously (an event every 10 ms, like a real mouse) for `ms`, running a
// frame after each event. Returns the last frame.
static OverlayActivity::Frame moveMouse(OverlayActivity& a, uint64_t& now, uint64_t ms, bool panelOpen = false) {
    const uint64_t end = now + ms;
    OverlayActivity::Frame f;
    for (; now <= end; now += 10) {
        a.mouseMoved(now);
        f = a.update(now, panelOpen);
    }
    now = end;
    return f;
}

// Frames without input for `ms`; returns the last one. `closed` records a close request.
static OverlayActivity::Frame idle(OverlayActivity& a, uint64_t& now, uint64_t ms, bool panelOpen = false,
                                   bool* closed = nullptr) {
    const uint64_t end = now + ms;
    OverlayActivity::Frame f;
    for (; now <= end; now += 16) {
        f = a.update(now, panelOpen);
        if (closed && f.closePanels) *closed = true;
    }
    now = end;
    return f;
}

static bool near(float a, float b) { return a > b - 0.02f && a < b + 0.02f; }

int main() {
    // ---------------------------------------------------------------- pointer
    {
        SCENARIO("[REQ-10][REQ-13] pointer: a short bump of the mouse shows nothing");
        OverlayActivity a;
        uint64_t now = 100000;
        const auto f = moveMouse(a, now, 400);
        CHECK(!f.visible && !f.cursor);
        CHECK(!idle(a, now, 500).visible);
        CHECK(!a.active());
    }
    {
        SCENARIO("[REQ-10][REQ-13] pointer: a second of moving the mouse shows the pointer and the controls, not earlier");
        OverlayActivity a;
        uint64_t now = 100000;
        auto f = moveMouse(a, now, 990);
        CHECK(!f.cursor && !f.visible);
        f = moveMouse(a, now, 20);
        CHECK(f.cursor && f.visible);
        CHECK(a.active());
    }
    {
        SCENARIO("[REQ-10][REQ-13] pointer: a pause of more than 0.3 s starts the count again");
        OverlayActivity a;
        uint64_t now = 100000;
        CHECK(!moveMouse(a, now, 700).cursor);
        now += OverlayActivity::kMotionGapMs + 50;
        CHECK(!moveMouse(a, now, 700).cursor);  // 1.4 s of moving in total, but never 1 s in one go
        CHECK(moveMouse(a, now, 400).cursor);
    }
    {
        SCENARIO("[REQ-10][REQ-13] pointer: short hesitations while moving still count");
        OverlayActivity a;
        uint64_t now = 100000;
        CHECK(!moveMouse(a, now, 500).cursor);
        now += 200;
        CHECK(moveMouse(a, now, 400).cursor);
    }
    {
        SCENARIO("[REQ-12][REQ-13] pointer: pointer and controls hide 2 s after the last input; then a second of moving is needed again");
        static_assert(OverlayActivity::kHideMs == 2000, "2 s, as asked");
        static_assert(OverlayActivity::kActivateMs == 1000, "1 s, as asked");
        OverlayActivity a;
        uint64_t now = 100000;
        CHECK(moveMouse(a, now, 1100).cursor);
        auto f = idle(a, now, 1900);
        CHECK(f.cursor && f.visible);
        f = idle(a, now, 200);
        CHECK(!f.cursor && !f.visible);
        CHECK(!moveMouse(a, now, 300).cursor);
        CHECK(moveMouse(a, now, 800).cursor);
    }
    {
        SCENARIO("[REQ-12] pointer: resting on the controls doesn't keep them: 2 s and they're gone");
        OverlayActivity a;
        uint64_t now = 100000;
        CHECK(moveMouse(a, now, 1000).visible);
        CHECK(!idle(a, now, 2100).visible);
    }
    {
        SCENARIO("[REQ-10] pointer: clicks and the wheel never show anything, but keep visible controls up");
        OverlayActivity a;
        uint64_t now = 100000;
        for (int i = 0; i < 10; ++i, now += 300) a.input(now);
        CHECK(!a.update(now, false).visible);
        CHECK(moveMouse(a, now, 1000).visible);
        for (int i = 0; i < 10; ++i, now += 1000) {
            a.input(now);
            const auto f = a.update(now, false);
            CHECK(f.visible && f.cursor);
        }
    }
    {
        SCENARIO("[REQ-10] pointer: deliberate keys (volume, mute, R) show the controls at once");
        OverlayActivity a;
        uint64_t now = 100000;
        a.show(now);
        const auto f = a.update(now, false);
        CHECK(f.visible && f.cursor);
    }

    // ---------------------------------------------------------------- panels / web hide
    {
        SCENARIO("[REQ-04][REQ-12] panels: an open panel stays up to 15 s; the pointer still hides after 2 s and comes back on the first move");
        OverlayActivity a;
        uint64_t now = 100000;
        a.show(now);  // R
        bool closed = false;
        auto f = idle(a, now, 2100, true, &closed);
        CHECK(f.visible && !f.cursor);
        a.mouseMoved(now);
        f = a.update(now, true);
        CHECK(f.visible && f.cursor);  // the operator is using the panel: no 1 s wait
        f = idle(a, now, OverlayActivity::kPanelIdleMs - 100, true, &closed);
        CHECK(f.visible && !f.cursor && !closed);
        f = idle(a, now, 200, true, &closed);
        CHECK(!f.visible && closed);
    }
    {
        SCENARIO("[REQ-04] panels: the web remote hides the controls and closes panels at once");
        OverlayActivity a;
        uint64_t now = 100000;
        a.show(now);
        CHECK(a.update(now, true).visible);
        a.dismiss();
        const auto f = a.update(now + 16, true);
        CHECK(!f.visible && !f.cursor);
        CHECK(f.closePanels);
        CHECK(a.dismissed());
        CHECK(!a.update(now + 32, false).closePanels);  // one request, not forever
    }
    {
        SCENARIO("[REQ-04][REQ-10] panels: after the web remote hid them, a bump of the mouse doesn't bring them back; 1 s does");
        OverlayActivity a;
        uint64_t now = 100000;
        CHECK(moveMouse(a, now, 1100).visible);
        a.dismiss();
        CHECK(!moveMouse(a, now, 500).visible);  // even while the mouse was moving when it was hidden
        CHECK(a.dismissed());
        CHECK(moveMouse(a, now, 600).visible);
        CHECK(!a.dismissed());
    }
    {
        SCENARIO("[REQ-04] panels: nothing may stay open while the controls are hidden");
        OverlayActivity a;
        CHECK(a.update(100000, true).closePanels);
    }

    // ---------------------------------------------------------------- fade
    {
        SCENARIO("[REQ-18] fade: the old picture stays (with its sound) until the new source's first frame, then they dissolve");
        TakeFade f;
        uint64_t now = 100000;
        f.begin(now, 500, true);
        auto m = f.update(now + 900);  // the new source is still connecting
        CHECK(f.active() && m.waiting);
        CHECK(m.outgoing == 1.0f && m.incoming == 0.0f && m.soundOut == 1.0f && m.soundIn == 0.0f);
        f.firstFrame(now + 1000);
        m = f.update(now + 1000);
        CHECK(!m.waiting && near(m.outgoing, 1.0f) && near(m.incoming, 0.0f));
        m = f.update(now + 1250);
        CHECK(near(m.outgoing, 0.5f) && near(m.incoming, 0.5f) && near(m.soundOut, 0.5f) && near(m.soundIn, 0.5f));
        CHECK(near(m.outgoing + m.incoming, 1.0f));  // a dissolve: never darker in the middle
        m = f.update(now + 1499);
        CHECK(!m.finished && m.incoming > 0.95f);
        m = f.update(now + 1500);
        CHECK(m.finished && !f.active() && m.incoming == 1.0f && m.outgoing == 0.0f);
        m = f.update(now + 1516);
        CHECK(!m.finished);  // reported once
    }
    {
        SCENARIO("[REQ-18] fade: a source that sends nothing within 3 s: the old picture fades to black, the new one fades in later");
        TakeFade f;
        uint64_t now = 100000;
        f.begin(now, 400, true);
        CHECK(f.update(now + TakeFade::kMaxWaitMs - 1).waiting);
        auto m = f.update(now + TakeFade::kMaxWaitMs);
        CHECK(!m.waiting && near(m.outgoing, 1.0f));
        m = f.update(now + TakeFade::kMaxWaitMs + 200);
        CHECK(near(m.outgoing, 0.5f));
        m = f.update(now + TakeFade::kMaxWaitMs + 400);
        CHECK(m.finished && !f.active());
        f.firstFrame(now + 5000);  // the source finally sends video
        CHECK(near(f.update(now + 5000).incoming, 0.0f));
        CHECK(near(f.update(now + 5200).incoming, 0.5f));
        CHECK(f.update(now + 5400).incoming == 1.0f);
    }
    {
        SCENARIO("[REQ-18] fade: taking None fades the old picture straight to black");
        TakeFade f;
        uint64_t now = 100000;
        f.begin(now, 1000, false);
        auto m = f.update(now + 500);
        CHECK(!m.waiting && near(m.outgoing, 0.5f) && near(m.soundOut, 0.5f));
        CHECK(f.update(now + 1000).finished);
    }
    {
        SCENARIO("[REQ-18] fade: from black (None) the new source's first frame fades in; its sound isn't held back");
        TakeFade f;
        uint64_t now = 100000;
        f.fadeInNext(500);
        auto m = f.update(now);
        CHECK(!f.active() && m.soundIn == 1.0f);
        f.firstFrame(now + 700);
        CHECK(near(f.update(now + 700).incoming, 0.0f));
        m = f.update(now + 950);
        CHECK(near(m.incoming, 0.5f) && m.soundIn == 1.0f);
        CHECK(f.update(now + 1200).incoming == 1.0f);
        f.firstFrame(now + 3000);  // only the first frame after a take fades in
        CHECK(f.update(now + 3000).incoming == 1.0f);
    }
    {
        SCENARIO("[REQ-18] fade: 0 ms is a cut, made when the new source's picture is there (never via black)");
        TakeFade f;
        f.fadeInNext(0);
        f.firstFrame(100000);
        CHECK(f.update(100000).incoming == 1.0f);
        f.begin(100000, 0, false);  // to None
        CHECK(f.update(100000).finished);
        f.begin(200000, 0, true);
        auto m = f.update(200400);
        CHECK(m.waiting && m.outgoing == 1.0f);  // the old picture holds while the new one connects
        f.firstFrame(200500);
        m = f.update(200500);
        CHECK(m.finished && m.incoming == 1.0f && m.outgoing == 0.0f);
    }
    {
        SCENARIO("[REQ-18] fade: a new take mid-fade starts over; reset() drops everything");
        TakeFade f;
        uint64_t now = 100000;
        f.begin(now, 500, true);
        f.firstFrame(now + 100);
        CHECK(near(f.update(now + 350).incoming, 0.5f));
        f.begin(now + 350, 500, true);  // main.cpp keeps the more visible picture as the old one
        CHECK(f.update(now + 400).waiting);
        f.reset();
        CHECK(!f.active() && f.update(now + 500).incoming == 1.0f);
    }

    // ---------------------------------------------------------------- links
    {
        SCENARIO("[REQ-01] links: open the remote page, signed in when a PIN is required");
        CHECK(remoteLink("http://192.168.1.20:8080", false, "4821") == "http://192.168.1.20:8080/");
        CHECK(remoteLink("http://192.168.1.20:8080", true, "4821") == "http://192.168.1.20:8080/#pin=4821");
        CHECK(remoteLink("http://studio.local:8081/", true, "0007") == "http://studio.local:8081/#pin=0007");
        CHECK(remoteLink("http://10.0.0.5:8080", true, "") == "http://10.0.0.5:8080/");
    }

    // ---------------------------------------------------------------- PIN default off, settings
    {
        SCENARIO("[REQ-09][REQ-02][REQ-05][REQ-18] PIN: off by default; always on top, silencing notifications and a 0.5 s fade are on");
        Settings s;
        CHECK(!s.remoteRequirePin);
        CHECK(s.alwaysOnTop);
        CHECK(s.silenceNotifications);
        CHECK(!s.silencedNotifications);
        CHECK(s.fadeMs == 500);
        Settings fresh;
        fresh.parse("");
        CHECK(!fresh.remoteRequirePin);
        CHECK(fresh.fadeMs == 500);
    }
    {
        SCENARIO("[REQ-09] PIN: settings saved by FeedView 1.0 (PIN on by default then) get the new default");
        Settings s;
        s.parse("# FeedView settings\nsource=X (Y)\nremote_pin=0666\nremote_require_pin=1\nvolume=40\n");
        CHECK(!s.remoteRequirePin);
        CHECK(s.remotePin == "0666");
        CHECK(s.source == "X (Y)" && s.volume == 40);
    }
    {
        SCENARIO("[REQ-09] PIN: a PIN switched on in this version stays on");
        Settings s;
        s.remoteRequirePin = true;
        s.remotePin = "4821";
        Settings back;
        back.parse(s.serialize());
        CHECK(back.remoteRequirePin);
        CHECK(contains(s.serialize(), "settings_version=2"));
    }
    {
        SCENARIO("[REQ-18] settings: round trip, including the live-production options, through a file; fade is clamped");
        Settings s;
        s.source = "STUDIO (Program)";
        s.alwaysOnTop = false;
        s.silenceNotifications = false;
        s.silencedNotifications = true;
        s.volume = 35;
        s.fadeMs = 1200;
        Settings back;
        back.parse(s.serialize());
        CHECK(back == s);
        const std::string path = "live_control_test_settings.ini";
        CHECK(s.save(path));
        Settings loaded;
        CHECK(loaded.load(path));
        CHECK(loaded == s);
        std::remove(path.c_str());
        Settings odd;
        odd.parse("fade_ms=-5\n");
        CHECK(odd.fadeMs == 0);
        odd.parse("fade_ms=999999\n");
        CHECK(odd.fadeMs == TakeFade::kMaxFadeMs);
    }

    // ---------------------------------------------------------------- state for the remote
    {
        SCENARIO("[REQ-07][REQ-08] state: the system volume and the OS audio outputs");
        RemoteSnapshot r;
        r.systemVolume = true;
        r.volume = 37;
        r.outputs = {{"{0.0.0.00000000}.{aa}", "Speakers (Realtek \"HD\")", true}, {"{0.0.0.00000000}.{bb}", "HDMI", false}};
        r.output = "{0.0.0.00000000}.{aa}";
        const std::string j = remoteStateJson(r);
        CHECK(contains(j, "\"system\":true"));
        CHECK(contains(j, "\"volume\":37"));
        CHECK(contains(j, "\"output\":\"{0.0.0.00000000}.{aa}\""));
        CHECK(contains(j, "\"name\":\"Speakers (Realtek \\\"HD\\\")\",\"default\":true"));
        CHECK(contains(j, "\"name\":\"HDMI\",\"default\":false"));
    }
    {
        SCENARIO("[REQ-04][REQ-06][REQ-12] state: controls/panels/pointer on screen, on top, notifications");
        RemoteSnapshot r;
        r.controlsVisible = true;
        r.cursorVisible = true;
        r.panel = "remote";
        r.onTop = true;
        r.alwaysOnTop = true;
        r.notificationsSupported = true;
        r.notificationsSilenced = true;
        r.notificationsMessage = "Notifications are off while FeedView runs";
        const std::string j = remoteStateJson(r);
        CHECK(contains(j, "\"controls\":{\"visible\":true,\"panel\":\"remote\",\"cursor\":true}"));
        CHECK(contains(j, "\"onTop\":true"));
        CHECK(contains(j, "\"alwaysOnTop\":true"));
        CHECK(contains(j, "\"notifications\":{\"supported\":true,\"permitted\":false,\"silenced\":true"));
    }
    {
        SCENARIO("[REQ-06][REQ-18] state: the picture on the output, a fade in progress and the fade setting");
        RemoteSnapshot r;
        r.outputPicture = true;
        r.fading = true;
        r.fadeWaiting = true;
        r.fadeFrom = "CAM (1)";
        r.fadeMs = 750;
        const std::string j = remoteStateJson(r);
        CHECK(contains(j, "\"picture\":true"));
        CHECK(contains(j, "\"fade\":{\"active\":true,\"waiting\":true,\"from\":\"CAM (1)\"}"));
        CHECK(contains(j, "\"fadeMs\":750"));
    }

    return finish();
}
