// Tiny key=value settings file kept in the per-user app data folder.
#pragma once

#include <string>

struct Settings {
    static constexpr int kVersion = 2;

    std::string source;      // last selected source, reconnected on launch
    std::string extraIps;    // comma separated discovery hosts
    int volume = 100;        // 0..100 (FeedView's own volume; used where the OS volume can't be)
    bool muted = false;
    int audioPair = 0;       // 0 = ch 1-2, 2 = ch 3-4, ...
    // Fullscreen display, remembered by name + left-to-right position (see display_manager.h).
    std::string displayName;
    int displayNth = 0;
    int displayIndex = -1;   // display number (0-based) when picked; fallback if name is empty
    bool startFullscreen = false;
    bool showInfo = false;
    // Hide FeedView's own messages on the fullscreen output (status stays in the web remote).
    bool cleanOutput = false;
    int fadeMs = 500;  // fade between sources, 0 = cut (see take_fade.h)
    // Live production: stay above every other window, and switch OS notifications off
    // while FeedView runs.
    bool alwaysOnTop = true;
    bool silenceNotifications = true;
    bool silencedNotifications = false;  // FeedView switched them off and hasn't restored them yet
    // Web remote
    bool remoteEnabled = true;
    int remotePort = 8080;
    std::string remotePin;  // generated on first start
    bool remoteRequirePin = false;

    bool parse(const std::string& text);
    std::string serialize() const;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool operator==(const Settings&) const = default;
};
