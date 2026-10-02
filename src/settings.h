// Tiny key=value settings file kept in the per-user app data folder.
#pragma once

#include <string>

struct Settings {
    std::string source;      // last selected source, reconnected on launch
    std::string extraIps;    // comma separated discovery hosts
    int volume = 100;        // 0..100
    bool muted = false;
    int audioPair = 0;       // 0 = ch 1-2, 2 = ch 3-4, ...
    int display = 0;         // index into the display list for fullscreen
    bool startFullscreen = false;
    bool showInfo = false;

    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool operator==(const Settings&) const = default;
};
