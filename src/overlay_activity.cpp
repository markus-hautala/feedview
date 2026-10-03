#include "overlay_activity.h"

void OverlayActivity::activate(uint64_t now) {
    active_ = true;
    dismissed_ = false;
    lastInput_ = now;
}

void OverlayActivity::mouseMoved(uint64_t now) {
    if (!streaking_ || now - lastMotion_ > kMotionGapMs) {
        streakStart_ = now;
        streaking_ = true;
    }
    lastMotion_ = now;
    if (active_)
        lastInput_ = now;
    else if (now - streakStart_ >= kActivateMs)
        activate(now);
}

void OverlayActivity::input(uint64_t now) {
    if (active_) lastInput_ = now;
}

void OverlayActivity::show(uint64_t now) { activate(now); }

void OverlayActivity::dismiss() {
    active_ = false;
    dismissed_ = true;
    closeRequested_ = true;
    streaking_ = false;
}

OverlayActivity::Frame OverlayActivity::update(uint64_t now, bool panelOpen) {
    Frame f;
    f.closePanels = closeRequested_;
    closeRequested_ = false;
    if (!active_) {
        f.closePanels = f.closePanels || panelOpen;  // nothing may stay open while hidden
        return f;
    }
    const uint64_t idle = now - lastInput_;
    if (idle <= kHideMs || (panelOpen && idle <= kPanelIdleMs)) {
        f.visible = true;
        f.cursor = idle <= kHideMs;
        return f;
    }
    active_ = false;
    streaking_ = false;
    f.closePanels = f.closePanels || panelOpen;
    return f;
}
