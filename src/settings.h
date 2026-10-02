// Tiny key=value settings file kept in the per-user app data folder.
#pragma once

#include <string>

struct Settings {
    std::string source;      // last selected source, reconnected on launch
    std::string extraIps;    // comma separated discovery hosts
    int volume = 100;        // 0..100
    bool muted = false;
    int audioPair = 0;       // 0 = ch 1-2, 2 = ch 3-4, ...
    // Fullscreen display, remembered by name + left-to-right position (see display_manager.h).
    std::string displayName;
    int displayNth = 0;
    int displayIndex = -1;   // display number (0-based) when picked; fallback if name is empty
    bool startFullscreen = false;
    bool showInfo = false;

    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool operator==(const Settings&) const = default;
};
