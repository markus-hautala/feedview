// What FeedView controls in the operating system during a show, so the whole computer can
// be run from the web remote:
//  * the system volume, mute and default audio output (FeedView plays to the default),
//  * OS notifications, which are switched off while FeedView runs,
//  * keeping the output window above every other window without taking keyboard focus
//    (FeedView reacts to single keys, so stealing focus from someone typing is dangerous).
//
// Implemented for Windows. On other systems available()/supported() report false and
// FeedView falls back to its own volume and leaves notifications alone.
//
// Not thread safe: use from the UI thread (SystemAudio holds COM objects of that thread).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace os {

struct AudioDevice {
    std::string id;    // OS endpoint id
    std::string name;  // "Speakers (Realtek(R) Audio)"
    bool isDefault = false;
};

class SystemAudio {
public:
    SystemAudio();
    ~SystemAudio();
    SystemAudio(const SystemAudio&) = delete;
    SystemAudio& operator=(const SystemAudio&) = delete;

    bool available() const;  // the OS volume and outputs can be controlled
    // Re-reads the OS state: volume/mute at most every 250 ms, outputs every 1.5 s
    // (someone may change them in the OS meanwhile). `force` reads everything now.
    void update(uint64_t nowMs, bool force = false);

    int volume() const;  // 0-100 as on the OS volume slider; -1 if unknown
    bool muted() const;
    const std::vector<AudioDevice>& outputs() const;  // active playback devices
    std::string defaultOutput() const;                // id; empty if none
    std::string defaultOutputName() const;

    bool setVolume(int volume);
    bool setMuted(bool muted);
    // Makes `id` the OS default playback device (as "Set as default device" does).
    bool setDefaultOutput(const std::string& id);
    std::string error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Silences OS notifications (Windows: the "Turn off toast notifications" user policy,
// applied immediately). Writing that policy needs a one-time permission given with admin
// rights (requestPermission); after that no prompts are needed.
class NotificationSilencer {
public:
    NotificationSilencer();
    ~NotificationSilencer();

    bool supported() const;
    bool permitted() const;   // can be switched without admin rights
    bool isSilenced() const;  // in effect now (by FeedView or by an administrator's policy)
    // Switches the policy on/off. The policy refresh it then signals runs in the
    // background (it can take a moment); `wait` blocks until it is done (use on exit).
    bool silence(bool on, bool wait = false);

    // Asks for the one-time permission (Windows shows a UAC prompt on this computer).
    // Runs in the background; poll permissionPending() and permitted().
    void requestPermission();
    bool permissionPending() const;
    std::string lastError() const;

private:
    struct Shared;
    std::shared_ptr<Shared> s_;
};

// The elevated half of requestPermission(): run as `FeedView --grant-notification-control
// <SID>` with admin rights. Lets the user with that SID write the notification policy key.
bool grantNotificationControl(const std::string& userSid);
std::string notificationPolicyValueForTests();  // "1", "0" or "" (missing)

// ---- Windows: `nativeWindow` is the HWND (SDL_PROP_WINDOW_WIN32_HWND_POINTER). Elsewhere
// these do nothing and return false.

// Brings the window above other windows (and restores it if minimized) without
// activating it. `topmost` also puts it in the always-on-top band.
void raiseWindow(void* nativeWindow, bool topmost);
// True if a visible window of another program (anything not created by the window's own
// thread) lies above it in the z-order and overlaps it - e.g. the taskbar or another
// always-on-top app over the fullscreen output.
bool coveredByOtherWindow(void* nativeWindow);
// For the fullscreen output: if covered, moves it back to the top of the always-on-top
// band. Returns true if it had to.
bool keepOnTop(void* nativeWindow);
bool isTopmost(void* nativeWindow);

}  // namespace os
