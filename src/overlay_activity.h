// When FeedView's own controls (top bar, info line) and the mouse pointer are on screen.
//
// On a live output an accidental bump of the mouse must not put anything over the picture,
// so the mouse has to keep moving for a moment (kActivateMs) before the pointer and the
// controls appear. Keys are deliberate and show them at once. Once visible, any input keeps
// them up; they hide again kHideMs after the last input. An open menu or panel (e.g. the
// Remote panel with its QR code) stays up to kPanelIdleMs without input - the pointer still
// hides after kHideMs, and comes straight back when the mouse moves. The web remote can
// hide everything at once with dismiss().
//
// Pure logic with no SDL, covered by tests/live_control_test.cpp.
#pragma once

#include <cstdint>

class OverlayActivity {
public:
    static constexpr uint64_t kActivateMs = 1000;    // continuous mouse movement needed
    static constexpr uint64_t kMotionGapMs = 300;    // a longer pause starts the count again
    static constexpr uint64_t kHideMs = 2000;        // pointer and controls hide after this
    static constexpr uint64_t kPanelIdleMs = 15000;  // open menus/panels close after this

    void mouseMoved(uint64_t nowMs);
    void input(uint64_t nowMs);  // click, wheel, key: keeps visible controls up, never shows them
    void show(uint64_t nowMs);   // a deliberate key press (volume, mute, R)
    void dismiss();              // hide now and close panels (web remote)

    struct Frame {
        bool visible = false;      // draw the controls this frame
        bool cursor = false;       // show the mouse pointer
        bool closePanels = false;  // close any open menu/panel this frame
    };
    // Once per frame. `panelOpen`: a menu or panel is open; it keeps the controls up (not the
    // pointer), but not beyond kPanelIdleMs without input.
    Frame update(uint64_t nowMs, bool panelOpen);

    bool active() const { return active_; }
    // Hidden by the web remote and nobody has used the controls since. FeedView then also
    // keeps the toolbar it normally shows while there's no picture off the output.
    bool dismissed() const { return dismissed_; }

private:
    void activate(uint64_t nowMs);

    bool active_ = false;
    bool dismissed_ = false;
    bool closeRequested_ = false;
    bool streaking_ = false;
    uint64_t streakStart_ = 0, lastMotion_ = 0, lastInput_ = 0;
};
