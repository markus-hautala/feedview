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

bool Settings::load(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return false;
    std::istringstream in(std::string(static_cast<const char*>(data), size));
    SDL_free(data);

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = line.substr(eq + 1);
        if (!val.empty() && val.back() == '\r') val.pop_back();
        if (key == "source") source = val;
        else if (key == "extra_ips") extraIps = trim(val);
        else if (key == "volume") volume = toInt(val, volume);
        else if (key == "muted") muted = toInt(val, 0) != 0;
        else if (key == "audio_pair") audioPair = toInt(val, 0);
        else if (key == "display") display = toInt(val, 0);
        else if (key == "start_fullscreen") startFullscreen = toInt(val, 0) != 0;
        else if (key == "show_info") showInfo = toInt(val, 0) != 0;
    }
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    if (audioPair < 0 || audioPair % 2) audioPair = 0;
    if (display < 0) display = 0;
    return true;
}

bool Settings::save(const std::string& path) const {
    std::ostringstream out;
    out << "# FeedView settings\n"
        << "source=" << source << "\n"
        << "extra_ips=" << extraIps << "\n"
        << "volume=" << volume << "\n"
        << "muted=" << (muted ? 1 : 0) << "\n"
        << "audio_pair=" << audioPair << "\n"
        << "display=" << display << "\n"
        << "start_fullscreen=" << (startFullscreen ? 1 : 0) << "\n"
        << "show_info=" << (showInfo ? 1 : 0) << "\n";
    const std::string text = out.str();

    // Write a temp file first so a crash never leaves a half-written settings file.
    const std::string tmp = path + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), text.data(), text.size())) return false;
    return SDL_RenamePath(tmp.c_str(), path.c_str());
}
