#include "settings.h"

#include <SDL3/SDL.h>

#include <sstream>

// File access goes through SDL so UTF-8 paths work on Windows too
// (e.g. a user folder like C:\Users\Mäkinen).

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

int toInt(const std::string& v, int fallback) {
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

bool Settings::parse(const std::string& text) {
    std::istringstream in(text);
    int version = 1;  // files from FeedView 1.0 have no version line
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = line.substr(eq + 1);
        if (!val.empty() && val.back() == '\r') val.pop_back();
        if (key == "settings_version") version = toInt(val, 1);
        else if (key == "source") source = val;
        else if (key == "extra_ips") extraIps = trim(val);
        else if (key == "volume") volume = toInt(val, volume);
        else if (key == "muted") muted = toInt(val, 0) != 0;
        else if (key == "audio_pair") audioPair = toInt(val, 0);
        else if (key == "display") displayIndex = toInt(val, -1);
        else if (key == "display_name") displayName = val;
        else if (key == "display_nth") displayNth = toInt(val, 0);
        else if (key == "start_fullscreen") startFullscreen = toInt(val, 0) != 0;
        else if (key == "show_info") showInfo = toInt(val, 0) != 0;
        else if (key == "clean_output") cleanOutput = toInt(val, 0) != 0;
        else if (key == "fade_ms") fadeMs = toInt(val, fadeMs);
        else if (key == "always_on_top") alwaysOnTop = toInt(val, 1) != 0;
        else if (key == "silence_notifications") silenceNotifications = toInt(val, 1) != 0;
        else if (key == "silenced_notifications") silencedNotifications = toInt(val, 0) != 0;
        else if (key == "remote_enabled") remoteEnabled = toInt(val, 1) != 0;
        else if (key == "remote_port") remotePort = toInt(val, 8080);
        else if (key == "remote_pin") remotePin = trim(val);
        else if (key == "remote_require_pin") remoteRequirePin = toInt(val, 0) != 0;
    }
    // 1.0 saved the PIN requirement even though nobody could have chosen it (it was the
    // default); the default is now off.
    if (version < 2) remoteRequirePin = false;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    if (audioPair < 0 || audioPair % 2) audioPair = 0;
    if (fadeMs < 0) fadeMs = 0;
    if (fadeMs > 10000) fadeMs = 10000;
    if (displayIndex < -1) displayIndex = -1;
    if (displayNth < 0) displayNth = 0;
    if (remotePort < 1 || remotePort > 65535) remotePort = 8080;
    if (remotePin.size() < 4 || remotePin.size() > 8 || remotePin.find_first_not_of("0123456789") != std::string::npos)
        remotePin.clear();
    return true;
}

std::string Settings::serialize() const {
    std::ostringstream out;
    out << "# FeedView settings\n"
        << "settings_version=" << kVersion << "\n"
        << "source=" << source << "\n"
        << "extra_ips=" << extraIps << "\n"
        << "volume=" << volume << "\n"
        << "muted=" << (muted ? 1 : 0) << "\n"
        << "audio_pair=" << audioPair << "\n"
        << "display=" << displayIndex << "\n"
        << "display_name=" << displayName << "\n"
        << "display_nth=" << displayNth << "\n"
        << "start_fullscreen=" << (startFullscreen ? 1 : 0) << "\n"
        << "show_info=" << (showInfo ? 1 : 0) << "\n"
        << "clean_output=" << (cleanOutput ? 1 : 0) << "\n"
        << "fade_ms=" << fadeMs << "\n"
        << "always_on_top=" << (alwaysOnTop ? 1 : 0) << "\n"
        << "silence_notifications=" << (silenceNotifications ? 1 : 0) << "\n"
        << "silenced_notifications=" << (silencedNotifications ? 1 : 0) << "\n"
        << "remote_enabled=" << (remoteEnabled ? 1 : 0) << "\n"
        << "remote_port=" << remotePort << "\n"
        << "remote_pin=" << remotePin << "\n"
        << "remote_require_pin=" << (remoteRequirePin ? 1 : 0) << "\n";
    return out.str();
}

bool Settings::load(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return false;
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return parse(text);
}

bool Settings::save(const std::string& path) const {
    const std::string text = serialize();
    // Write a temp file first so a crash never leaves a half-written settings file.
    const std::string tmp = path + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), text.data(), text.size())) return false;
    return SDL_RenamePath(tmp.c_str(), path.c_str());
}
