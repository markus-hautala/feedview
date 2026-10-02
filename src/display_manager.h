// Keeps FeedView on the right screen while monitors are plugged, unplugged, rearranged or
// change resolution - without restarting the app.
//
// The logic lives here, free of SDL, so it can be exercised with a simulated multi-monitor
// system in tests/display_manager_test.cpp. main.cpp provides the SDL implementation of
// DisplayPlatform.
//
// Behaviour in short:
//  * The chosen display is remembered by name + left-to-right position among monitors of the
//    same name, never by a list index (indices shift whenever something is plugged in).
//  * Displays are numbered 1..N from left to right (top to bottom on ties), which is what
//    operators see on the "Identify" screens.
//  * Fullscreen is a user intent. If the target screen disappears, FeedView drops to a window
//    instead of covering whatever screen the OS moved it to (often the operator's own
//    laptop), and goes back to fullscreen there as soon as the screen returns.
//  * Resolution / arrangement changes re-fit the fullscreen window automatically.
//  * Leaving fullscreen through the OS (macOS green button, Win+Down...) is respected; only
//    when it coincides with a display change is it treated as the OS's doing and repaired.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct DisplayRect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct DisplayInfo {
    uint32_t id = 0;  // platform id; valid while the display stays connected
    std::string name;
    DisplayRect bounds;
    int nth = 0;  // 0-based position among displays with the same name (left to right)
};

// What the user picked; persisted in settings.
struct DisplayTarget {
    std::string name;
    int nth = 0;
    int index = -1;  // number (0-based) at the time it was picked; used only if name is empty
    bool operator==(const DisplayTarget&) const = default;
};

class DisplayPlatform {
public:
    virtual ~DisplayPlatform() = default;
    virtual std::vector<DisplayInfo> displays() = 0;  // any order; nth is filled in by the manager
    virtual bool windowFullscreen() = 0;
    virtual uint32_t windowDisplay() = 0;  // display the window is on, 0 if unknown
    virtual DisplayRect windowRect() = 0;
    virtual void enterFullscreen(uint32_t displayId) = 0;  // borderless, covering that display
    virtual void leaveFullscreen() = 0;
    virtual void moveWindowTo(uint32_t displayId) = 0;  // windowed, centred on that display
    virtual void notify(const std::string& message) = 0;
};

// Sorts left to right, numbers same-named displays and returns the result.
std::vector<DisplayInfo> arrangeDisplays(std::vector<DisplayInfo> list);
// Index of the target in an arranged list, or -1 if it is not connected.
int findDisplay(const std::vector<DisplayInfo>& arranged, const DisplayTarget& target);

class DisplayManager {
public:
    explicit DisplayManager(DisplayPlatform& platform) : p_(platform) {}

    // ---- Setup
    void setTarget(const DisplayTarget& t) { target_ = t; }
    const DisplayTarget& target() const { return target_; }
    // Call once at start-up. With `fullscreen`, goes fullscreen on the target, or waits for
    // it if it is not connected yet (e.g. the projector is still warming up).
    void start(uint64_t nowMs, bool fullscreen);

    // ---- Operator actions
    void setFullscreen(bool on, uint64_t nowMs);  // F / Esc / double-click / button
    // F key: while waiting for a missing screen it means "fullscreen here instead".
    void toggleFullscreen(uint64_t nowMs) { setFullscreen(parked_ || !wantFullscreen_, nowMs); }
    // Picks display `index` (0-based number). With `goFullscreen` it also goes fullscreen;
    // otherwise it keeps the current mode (moving the window if it is windowed).
    void chooseDisplay(int index, bool goFullscreen, uint64_t nowMs);
    void nextDisplay(uint64_t nowMs);

    // ---- Platform events
    void displaysChanged(uint64_t nowMs);      // added / removed / moved / mode / orientation
    void windowDisplayChanged(uint64_t nowMs);  // the window now sits on another display
    void windowLeftFullscreen(uint64_t nowMs);  // fullscreen ended without us asking
    void update(uint64_t nowMs);                // every frame: debounced repairs + watchdog

    // ---- State for the UI
    const std::vector<DisplayInfo>& displays() const { return list_; }
    int targetIndex() const { return findDisplay(list_, target_); }
    bool wantFullscreen() const { return wantFullscreen_; }
    bool waitingForTarget() const { return parked_; }  // fullscreen wanted but screen missing
    std::string label(int index) const;                // "2: DELL U2720Q (2560x1440)"
    std::string targetLabel() const;

    static constexpr uint64_t kSettleMs = 750;      // displays send bursts of events
    static constexpr uint64_t kOsChangeWindowMs = 3000;
    static constexpr int kMaxAttempts = 3;          // per arrangement, then stop trying

private:
    void refresh();
    void repair(uint64_t nowMs);
    bool placedCorrectly(int index);
    bool windowVisible();
    int windowDisplayIndex();
    std::string signature() const;
    void placeFullscreen(int index, uint64_t nowMs);
    DisplayTarget targetFromIndex(int index) const;
    bool recentTopologyChange(uint64_t nowMs) const {
        return everTopologyChange_ && nowMs - lastTopologyChange_ < kOsChangeWindowMs;
    }
    bool recentPlacement(uint64_t nowMs) const { return lastPlacement_ && nowMs - lastPlacement_ < 1500; }

    DisplayPlatform& p_;
    DisplayTarget target_;
    std::vector<DisplayInfo> list_;
    bool wantFullscreen_ = false;
    bool parked_ = false;
    uint64_t repairAt_ = 0;  // 0 = nothing scheduled
    uint64_t lastTopologyChange_ = 0;
    uint64_t lastPlacement_ = 0;
    uint64_t lastWatchdog_ = 0;
    bool everTopologyChange_ = false;
    std::map<std::string, int> attempts_;
};
