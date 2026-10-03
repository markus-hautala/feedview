#include "display_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

std::vector<DisplayInfo> arrangeDisplays(std::vector<DisplayInfo> list) {
    std::stable_sort(list.begin(), list.end(), [](const DisplayInfo& a, const DisplayInfo& b) {
        if (a.bounds.x != b.bounds.x) return a.bounds.x < b.bounds.x;
        if (a.bounds.y != b.bounds.y) return a.bounds.y < b.bounds.y;
        return a.id < b.id;
    });
    std::map<std::string, int> seen;
    for (auto& d : list) d.nth = seen[d.name]++;
    return list;
}

int findDisplay(const std::vector<DisplayInfo>& list, const DisplayTarget& t) {
    if (list.empty()) return -1;
    if (t.name.empty()) return (t.index >= 0 && t.index < int(list.size())) ? t.index : -1;
    int sameName = -1;
    for (int i = 0; i < int(list.size()); ++i) {
        if (list[i].name != t.name) continue;
        if (list[i].nth == t.nth) return i;
        sameName = i;  // identical models are told apart only by their left-to-right order
    }
    return sameName;
}

// ---------------------------------------------------------------------------------------

void DisplayManager::refresh() { list_ = arrangeDisplays(p_.displays()); }

std::string DisplayManager::signature() const {
    std::string s;
    for (const auto& d : list_) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%u@%d,%d,%dx%d;", d.id, d.bounds.x, d.bounds.y, d.bounds.w, d.bounds.h);
        s += buf;
    }
    return s;
}

std::string DisplayManager::label(int i) const {
    if (i < 0 || i >= int(list_.size())) return std::string();
    char buf[64];
    std::snprintf(buf, sizeof buf, " (%dx%d)", list_[i].bounds.w, list_[i].bounds.h);
    return std::to_string(i + 1) + ": " + list_[i].name + buf;
}

std::string DisplayManager::targetLabel() const {
    int t = targetIndex();
    if (t >= 0) return label(t);
    if (!target_.name.empty()) return target_.name + " (disconnected)";
    if (target_.index >= 0) return "Display " + std::to_string(target_.index + 1) + " (disconnected)";
    return "Choose display";
}

DisplayTarget DisplayManager::targetFromIndex(int i) const {
    return DisplayTarget{list_[i].name, list_[i].nth, i};
}

int DisplayManager::windowDisplayIndex() {
    const uint32_t id = p_.windowDisplay();
    for (int i = 0; i < int(list_.size()); ++i)
        if (list_[i].id == id) return i;
    return -1;
}

bool DisplayManager::windowVisible() {
    const DisplayRect r = p_.windowRect();
    for (const auto& d : list_) {
        int ow = std::min(r.x + r.w, d.bounds.x + d.bounds.w) - std::max(r.x, d.bounds.x);
        int oh = std::min(r.y + r.h, d.bounds.y + d.bounds.h) - std::max(r.y, d.bounds.y);
        // Enough of the window (title bar area included) must be on a screen to grab it.
        if (ow >= std::min(100, r.w) && oh >= std::min(50, r.h)) return true;
    }
    return false;
}

bool DisplayManager::placedCorrectly(int i) {
    if (!p_.windowFullscreen() || p_.windowDisplay() != list_[i].id) return false;
    const DisplayRect r = p_.windowRect();
    const DisplayRect& b = list_[i].bounds;
    return std::abs(r.x - b.x) <= 2 && std::abs(r.y - b.y) <= 2 && std::abs(r.w - b.w) <= 2 &&
           std::abs(r.h - b.h) <= 2;
}

void DisplayManager::placeFullscreen(int i, uint64_t now) {
    // Never loop forever on a platform that won't cooperate: a few tries per arrangement.
    int& tries = attempts_[signature()];
    if (tries >= kMaxAttempts) return;
    ++tries;
    p_.enterFullscreen(list_[i].id);
    lastPlacement_ = now;
}

void DisplayManager::start(uint64_t now, bool fullscreen) {
    refresh();
    if (!fullscreen) return;
    wantFullscreen_ = true;
    int t = targetIndex();
    if (t >= 0) {
        placeFullscreen(t, now);
    } else if (!target_.name.empty() || target_.index >= 0) {
        parked_ = true;
        p_.notify("Waiting for display \"" + (target_.name.empty() ? std::to_string(target_.index + 1) : target_.name) +
                  "\" - FeedView goes fullscreen there as soon as it is connected");
    } else if (!list_.empty()) {
        int w = std::max(0, windowDisplayIndex());
        target_ = targetFromIndex(w);
        placeFullscreen(w, now);
    }
}

void DisplayManager::setFullscreen(bool on, uint64_t now) {
    attempts_.clear();
    if (!on) {
        wantFullscreen_ = false;
        parked_ = false;
        if (p_.windowFullscreen()) p_.leaveFullscreen();
        lastPlacement_ = now;
        return;
    }
    refresh();
    int t = targetIndex();
    if (t < 0) {
        // Target missing (or never chosen): go fullscreen where the window is and make
        // that the new target - the operator is asking for it right now.
        t = windowDisplayIndex();
        if (t < 0) t = list_.empty() ? -1 : 0;
        if (t < 0) return;
        const bool hadTarget = parked_;
        target_ = targetFromIndex(t);
        if (hadTarget) p_.notify("Fullscreen on " + label(t));
    }
    wantFullscreen_ = true;
    parked_ = false;
    placeFullscreen(t, now);
}

void DisplayManager::chooseDisplay(int i, bool goFullscreen, uint64_t now) {
    refresh();
    if (i < 0 || i >= int(list_.size())) {
        p_.notify(list_.size() <= 1 ? std::string("Only one display is connected")
                                    : "There is no display " + std::to_string(i + 1));
        return;
    }
    attempts_.clear();
    target_ = targetFromIndex(i);
    parked_ = false;
    if (goFullscreen) wantFullscreen_ = true;
    if (wantFullscreen_) {
        placeFullscreen(i, now);
        p_.notify("Fullscreen on " + label(i));
    } else {
        p_.moveWindowTo(list_[i].id);
        lastPlacement_ = now;
        p_.notify("Moved to " + label(i));
    }
}

void DisplayManager::nextDisplay(uint64_t now) {
    refresh();
    const int n = int(list_.size());
    if (n < 2) {
        p_.notify("Only one display is connected");
        return;
    }
    int t = targetIndex();
    if (t < 0) t = windowDisplayIndex();
    chooseDisplay((t + 1 + n) % n, false, now);
}

void DisplayManager::displaysChanged(uint64_t now) {
    everTopologyChange_ = true;
    lastTopologyChange_ = now;
    repairAt_ = now + kSettleMs;  // wait for the burst of events to settle
    refresh();                    // the UI shows the new list right away
}

void DisplayManager::windowDisplayChanged(uint64_t now) {
    if (wantFullscreen_ || parked_) {
        if (!recentPlacement(now) && !repairAt_) repairAt_ = now + kSettleMs;
        return;
    }
    // A windowed FeedView dragged to another screen: that screen becomes the target, so F
    // goes fullscreen where the operator just put the window. Ignore moves made by the OS
    // while displays are changing, and our own moves.
    if (recentTopologyChange(now) || recentPlacement(now)) return;
    refresh();
    int w = windowDisplayIndex();
    if (w >= 0) target_ = targetFromIndex(w);
}

void DisplayManager::windowLeftFullscreen(uint64_t now) {
    if (!wantFullscreen_ || parked_ || recentPlacement(now)) return;
    if (recentTopologyChange(now)) {
        if (!repairAt_) repairAt_ = now + kSettleMs;  // the OS did it: put it back
    } else {
        wantFullscreen_ = false;  // the operator did it (green button, Win+Down...): respect it
    }
}

void DisplayManager::update(uint64_t now) {
    if (repairAt_ && now >= repairAt_) {
        repairAt_ = 0;
        repair(now);
        return;
    }
    // Watchdog for platforms that miss an event: once a second, cheap checks only.
    if (now - lastWatchdog_ < 1000 || recentPlacement(now) || repairAt_) return;
    lastWatchdog_ = now;
    refresh();
    if (wantFullscreen_ && !parked_) {
        int t = targetIndex();
        if (t < 0 || !placedCorrectly(t)) repair(now);
    } else if (parked_) {
        if (targetIndex() >= 0) repair(now);
    } else if (!p_.windowFullscreen() && !windowVisible()) {
        repair(now);
    }
}

void DisplayManager::repair(uint64_t now) {
    refresh();
    const int t = targetIndex();
    if (wantFullscreen_) {
        if (t >= 0) {
            const bool wasParked = parked_;
            parked_ = false;
            if (!placedCorrectly(t)) placeFullscreen(t, now);
            if (wasParked) p_.notify(label(t) + " is back - fullscreen restored");
            return;
        }
        // Target screen gone: don't cover whatever screen the OS moved us to (often the
        // operator's own). Wait in a window until it comes back.
        if (!parked_) {
            parked_ = true;
            p_.notify("\"" + (target_.name.empty() ? targetLabel() : target_.name) +
                      "\" disconnected. FeedView returns to it automatically.");
        }
        if (p_.windowFullscreen()) {
            p_.leaveFullscreen();
            lastPlacement_ = now;
        }
    }
    if (!p_.windowFullscreen() && !windowVisible() && !list_.empty()) {
        p_.moveWindowTo(list_[0].id);  // the window was left off-screen
        lastPlacement_ = now;
    }
}
