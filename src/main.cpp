// FeedView - a small cross-platform player for NDI(R) video sources.
//
// NDI(R) is a registered trademark of Vizrt NDI AB. FeedView is not affiliated with or
// endorsed by Vizrt NDI AB.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "display_manager.h"
#include "ndi_io.h"
#include "ndi_runtime.h"
#include "net_info.h"
#include "qrcodegen.hpp"
#include "remote_server.h"
#include "remote_view.h"
#include "settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifndef FEEDVIEW_VERSION
#define FEEDVIEW_VERSION "dev"
#endif

extern const unsigned char kUiFontData[];
extern const unsigned int kUiFontDataSize;
extern const unsigned char kWebPage[];
extern const unsigned int kWebPageSize;

namespace {

constexpr const char* kAppName = "FeedView";

#ifndef FEEDVIEW_IDENTIFY_MIN_DISPLAYS
#define FEEDVIEW_IDENTIFY_MIN_DISPLAYS 2  // tests on a single-screen machine build with 1
#endif
constexpr double kOverlayHideSeconds = 3.0;

// ---------------------------------------------------------------------------------------
// Command line

struct Options {
    std::string source;
    bool sourceSet = false;
    std::string extraIps;
    bool extraIpsSet = false;
    bool fullscreen = false;
    int display = -1;  // 1-based on the command line, stored 0-based
    int volume = -1;
    bool mute = false;
    bool listSources = false;
    double listSeconds = 3.0;
    bool help = false;
    bool version = false;
    int remotePort = -1;
    bool noRemote = false;
    std::string error;
};

const char* kUsage =
    "Usage: FeedView [options]\n"
    "\n"
    "  --source NAME       Connect to this NDI source, e.g. \"STUDIO-PC (Program)\"\n"
    "  --fullscreen, -f    Start in fullscreen\n"
    "  --display N         Fullscreen on display N (numbered left to right, 1 = leftmost)\n"
    "  --extra-ips LIST    Comma separated IPs to search for sources on other subnets\n"
    "  --volume N          Volume 0-100\n"
    "  --mute              Start muted\n"
    "  --list-sources[=S]  Print the sources found within S seconds (default 3) and exit\n"
    "  --remote-port N     Port for the web remote (default 8080, next free one if taken)\n"
    "  --no-remote         Turn the web remote off for this run\n"
    "  --version           Print version and exit\n"
    "  --help              Show this help\n"
    "\n"
    "Keys: F/F11 fullscreen, Esc leave fullscreen, 1-9 pick source, 0 disconnect,\n"
    "      D show display numbers (then click one or press its number),\n"
    "      Ctrl+1-9 fullscreen on display N, M mute, Up/Down volume, I info overlay.\n";

Options parseArgs(int argc, char** argv) {
    Options o;
    auto value = [&](int& i, const std::string& arg, const char* name, std::string& out) -> bool {
        std::string prefix = std::string(name) + "=";
        if (arg.rfind(prefix, 0) == 0) {
            out = arg.substr(prefix.size());
            return true;
        }
        if (arg == name) {
            if (i + 1 >= argc) {
                o.error = std::string(name) + " needs a value";
                return true;
            }
            out = argv[++i];
            return true;
        }
        return false;
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        std::string v;
        if (value(i, a, "--source", v)) {
            o.source = v;
            o.sourceSet = true;
        } else if (value(i, a, "--extra-ips", v)) {
            o.extraIps = v;
            o.extraIpsSet = true;
        } else if (value(i, a, "--display", v)) {
            o.display = std::max(1, std::atoi(v.c_str())) - 1;
        } else if (value(i, a, "--volume", v)) {
            o.volume = std::clamp(std::atoi(v.c_str()), 0, 100);
        } else if (value(i, a, "--remote-port", v)) {
            o.remotePort = std::clamp(std::atoi(v.c_str()), 1, 65535);
        } else if (a == "--no-remote") {
            o.noRemote = true;
        } else if (a == "--fullscreen" || a == "-f") {
            o.fullscreen = true;
        } else if (a == "--mute") {
            o.mute = true;
        } else if (a == "--list-sources") {
            o.listSources = true;
        } else if (a.rfind("--list-sources=", 0) == 0) {
            o.listSources = true;
            o.listSeconds = std::max(0.5, std::atof(a.c_str() + 15));
        } else if (a == "--help" || a == "-h" || a == "/?") {
            o.help = true;
        } else if (a == "--version") {
            o.version = true;
        } else if (a.rfind("-psn_", 0) == 0) {
            // macOS Finder passes a process serial number; ignore it.
        } else {
            o.error = "Unknown option: " + a;
        }
    }
    return o;
}

// A Windows GUI app has no console; borrow the parent's so --help/--list-sources print.
// If output is already redirected (a pipe or file, e.g. in scripts), leave it alone.
void attachConsoleIfAny() {
#if defined(_WIN32)
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
}

// ---------------------------------------------------------------------------------------
// Audio output: jitter buffer + clock-drift compensation
//
// The sender's audio clock and the local sound card never run at exactly the same speed
// (typically 10-100 ppm apart). Left alone the buffer slowly drains (dropouts) or grows
// (audio drifts behind the picture). We watch how full the buffer is just before each new
// block arrives and nudge SDL's resampler by at most +-0.5 % to hold it at a small target,
// which is inaudible and keeps long-running monitors glitch-free.

#ifndef FEEDVIEW_AUDIO_MAX_RATE_ADJUST
#define FEEDVIEW_AUDIO_MAX_RATE_ADJUST 0.005  // +-0.5 %: covers real sound cards, inaudible
#endif

class AudioOut {
public:
    static constexpr double kMaxRateAdjust = FEEDVIEW_AUDIO_MAX_RATE_ADJUST;

    bool open() {
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_) return false;
        inputRate_ = 48000;
        SDL_ResumeAudioStreamDevice(stream_);
        return true;
    }
    void close() {
        if (stream_) SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    bool ok() const { return stream_ != nullptr; }

    // Called from the NDI capture thread (SDL audio streams are thread safe). SDL resamples
    // whatever rate the source sends to the device rate.
    void push(const float* stereo, int frames, int rate) {
        if (!stream_ || frames <= 0 || rate <= 0) return;
        if (reset_.exchange(false) || rate != inputRate_) {
            SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate};
            SDL_ClearAudioStream(stream_);
            SDL_SetAudioStreamFormat(stream_, &spec, nullptr);
            SDL_SetAudioStreamFrequencyRatio(stream_, 1.0f);
            inputRate_ = rate;
            ratio_ = 1.0;
            avgChunkMs_ = 0.0;
            avgLowMs_ = -1.0;
        }
        const double bytesPerMs = rate * 2.0 * sizeof(float) / 1000.0;
        const double chunkMs = frames * 1000.0 / rate;
        avgChunkMs_ = avgChunkMs_ <= 0 ? chunkMs : avgChunkMs_ * 0.9 + chunkMs * 0.1;
        // Headroom to keep when a block arrives: enough for network jitter and bursty senders.
        const double targetMs = std::clamp(avgChunkMs_ * 1.5, 30.0, 150.0);

        double lowMs = SDL_GetAudioStreamQueued(stream_) / bytesPerMs;
        if (lowMs > targetMs + 300.0) {
            // Far behind (a burst after a network hiccup): drop it rather than lag the picture.
            SDL_ClearAudioStream(stream_);
            lowMs = 0.0;
            ++resyncs_;
        }
        if (lowMs <= 0.0) {
            // Starved: lead with silence up to the target so playback restarts smoothly.
            silence_.assign(size_t(targetMs * bytesPerMs / sizeof(float)) & ~size_t(1), 0.0f);
            SDL_PutAudioStreamData(stream_, silence_.data(), int(silence_.size() * sizeof(float)));
            lowMs = targetMs;
            avgLowMs_ = -1.0;
            ++underruns_;
        }
        SDL_PutAudioStreamData(stream_, stereo, frames * 2 * int(sizeof(float)));

        avgLowMs_ = avgLowMs_ < 0 ? lowMs : avgLowMs_ * 0.95 + lowMs * 0.05;
        const double error = avgLowMs_ - targetMs;  // > 0: too much buffered -> play a bit faster
        const double ratio = 1.0 + std::clamp(error * 0.0001, -kMaxRateAdjust, kMaxRateAdjust);
        if (std::fabs(ratio - ratio_) > 0.00002) {
            SDL_SetAudioStreamFrequencyRatio(stream_, float(ratio));
            ratio_ = ratio;
        }
        bufferMs_ = float(avgLowMs_ + chunkMs);
        ratioPpm_ = float((ratio_ - 1.0) * 1e6);
    }
    void setGain(float g) {
        if (stream_) SDL_SetAudioStreamGain(stream_, g);
    }
    // Drop queued audio (e.g. when switching sources); the capture thread resets its state.
    void flush() {
        if (stream_) SDL_ClearAudioStream(stream_);
        reset_ = true;
    }
    float bufferMs() const { return bufferMs_; }
    float driftPpm() const { return ratioPpm_; }
    int underruns() const { return underruns_; }
    int resyncs() const { return resyncs_; }

private:
    SDL_AudioStream* stream_ = nullptr;
    std::atomic<int> inputRate_{0};
    std::atomic<bool> reset_{false};
    std::atomic<int> resyncs_{0};
    std::atomic<int> underruns_{0};
    std::atomic<float> bufferMs_{0.0f};
    std::atomic<float> ratioPpm_{0.0f};
    // Capture-thread only:
    double ratio_ = 1.0, avgChunkMs_ = 0.0, avgLowMs_ = -1.0;
    std::vector<float> silence_;
};

// ---------------------------------------------------------------------------------------
// Helpers

std::string formatRate(int n, int d) {
    if (n <= 0 || d <= 0) return "";
    double fps = double(n) / double(d);
    char buf[32];
    if (std::fabs(fps - std::round(fps)) < 0.001)
        std::snprintf(buf, sizeof buf, "%dp", int(std::round(fps)));
    else
        std::snprintf(buf, sizeof buf, "%.2fp", fps);
    return buf;
}

std::string pairLabel(int first, int channels) {
    if (channels == 1) return "Mono";
    char buf[32];
    std::snprintf(buf, sizeof buf, "Ch %d-%d", first + 1, first + 2);
    return buf;
}

float volumeToGain(int volume, bool muted) {
    if (muted) return 0.0f;
    float v = std::clamp(volume, 0, 100) / 100.0f;
    return v * v;  // roughly perceptual
}

// Draws text centred at `center` using the UI font at `scale` x the normal size.
void centeredText(ImDrawList* dl, ImVec2 center, const char* text, float scale, ImU32 color) {
    ImFont* font = ImGui::GetFont();
    float size = ImGui::GetFontSize() * scale;
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(std::floor(center.x - ts.x * 0.5f), std::floor(center.y - ts.y * 0.5f)), color,
                text);
}

// Puts the next widget on the same line if `width` still fits, otherwise starts a new line.
void sameLineIfFits(float width) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
}

// ---------------------------------------------------------------------------------------
// UI scale (follows the monitor the window is on)

float contentScaleFor(SDL_Window* w) {
    SDL_DisplayID d = w ? SDL_GetDisplayForWindow(w) : 0;
    float s = d ? SDL_GetDisplayContentScale(d) : 0.0f;
    if (s <= 0.0f) s = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    return std::max(1.0f, s);
}

void applyUiStyle(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.10f, 0.86f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.12f, 0.97f);
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    style.FontSizeBase = 16.0f;
}

// ---------------------------------------------------------------------------------------
// SDL side of the display manager

class SdlDisplayPlatform : public DisplayPlatform {
public:
    SdlDisplayPlatform(SDL_Window* w, std::function<void(const std::string&)> notify)
        : w_(w), notify_(std::move(notify)) {}

    std::vector<DisplayInfo> displays() override {
        std::vector<DisplayInfo> out;
        int n = 0;
        if (SDL_DisplayID* ids = SDL_GetDisplays(&n)) {
            for (int i = 0; i < n; ++i) {
                SDL_Rect r{};
                if (!SDL_GetDisplayBounds(ids[i], &r)) continue;
                const char* nm = SDL_GetDisplayName(ids[i]);
                DisplayInfo d;
                d.id = ids[i];
                d.name = (nm && *nm) ? nm : "Display";
                d.bounds = {r.x, r.y, r.w, r.h};
                out.push_back(d);
            }
            SDL_free(ids);
        }
        return out;
    }
    bool windowFullscreen() override { return (SDL_GetWindowFlags(w_) & SDL_WINDOW_FULLSCREEN) != 0; }
    uint32_t windowDisplay() override { return SDL_GetDisplayForWindow(w_); }
    DisplayRect windowRect() override {
        int x = 0, y = 0, ww = 0, hh = 0;
        SDL_GetWindowPosition(w_, &x, &y);
        SDL_GetWindowSize(w_, &ww, &hh);
        return {x, y, ww, hh};
    }
    void enterFullscreen(uint32_t id) override {
        // Leave first, so a changed resolution/arrangement is picked up cleanly.
        if (windowFullscreen()) {
            SDL_SetWindowFullscreen(w_, false);
            SDL_SyncWindow(w_);
        }
        SDL_SetWindowPosition(w_, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
        SDL_SyncWindow(w_);
        SDL_SetWindowFullscreenMode(w_, nullptr);  // borderless "desktop" fullscreen
        SDL_SetWindowFullscreen(w_, true);
        SDL_SyncWindow(w_);
    }
    void leaveFullscreen() override {
        SDL_SetWindowFullscreen(w_, false);
        SDL_SyncWindow(w_);
    }
    void moveWindowTo(uint32_t id) override {
        if (windowFullscreen()) leaveFullscreen();
        SDL_Rect b{};
        int ww = 0, hh = 0;
        SDL_GetWindowSize(w_, &ww, &hh);
        if (SDL_GetDisplayUsableBounds(id, &b) && b.w > 0 && (ww > b.w * 9 / 10 || hh > b.h * 9 / 10)) {
            ww = b.w * 2 / 3;  // keep it a manageable window on a smaller screen
            hh = ww * 9 / 16;
            SDL_SetWindowSize(w_, ww, hh);
        }
        SDL_SetWindowPosition(w_, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
        SDL_SyncWindow(w_);
    }
    void notify(const std::string& m) override { notify_(m); }

private:
    SDL_Window* w_;
    std::function<void(const std::string&)> notify_;
};

// ---------------------------------------------------------------------------------------
// "Identify": a big number on every screen. Click one (or press its number) to put
// FeedView fullscreen there. The screen FeedView already fills shows the same card inside
// the main window instead, because a fullscreen window can sit above "always on top" ones.

class IdentifyOverlay {
public:
    ~IdentifyOverlay() { hide(); }
    bool active() const { return !items_.empty() || skipIndex_ >= 0; }
    int skipIndex() const { return skipIndex_; }

    void show(const std::vector<DisplayInfo>& list, int targetIndex, SDL_DisplayID skip, Uint64 now) {
        hide();
        for (int i = 0; i < int(list.size()); ++i) {
            const DisplayInfo& d = list[i];
            if (d.id == skip) {
                skipIndex_ = i;
                continue;
            }
            const float scale = std::max(1.0f, SDL_GetDisplayContentScale(d.id));
            const int w = std::min(int(480 * scale), d.bounds.w * 3 / 4);
            const int h = w * 5 / 8;
            SDL_Window* win = SDL_CreateWindow("FeedView display", w, h,
                                               SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_UTILITY |
                                                   SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_HIDDEN);
            if (!win) continue;
            SDL_SetWindowPosition(win, d.bounds.x + (d.bounds.w - w) / 2, d.bounds.y + (d.bounds.h - h) / 2);
            SDL_Renderer* r = SDL_CreateRenderer(win, SDL_SOFTWARE_RENDERER);
            if (!r) {
                SDL_DestroyWindow(win);
                continue;
            }
            SDL_ShowWindow(win);
            items_.push_back({win, r, i, d.name, resolution(d), i == targetIndex});
        }
        targetIndex_ = targetIndex;
        until_ = now + 10000;
    }
    void hide() {
        for (auto& it : items_) {
            SDL_DestroyRenderer(it.r);
            SDL_DestroyWindow(it.w);
        }
        items_.clear();
        skipIndex_ = -1;
    }
    void update(Uint64 now) {
        if (active() && now > until_) hide();
    }
    int hitTest(SDL_WindowID id) const {
        for (const auto& it : items_)
            if (SDL_GetWindowID(it.w) == id) return it.index;
        return -1;
    }
    void render() {
        for (auto& it : items_) drawCard(it);
    }
    static std::string resolution(const DisplayInfo& d) {
        return std::to_string(d.bounds.w) + " x " + std::to_string(d.bounds.h);
    }

private:
    struct Item {
        SDL_Window* w;
        SDL_Renderer* r;
        int index;
        std::string name, res;
        bool target;
    };

    static void drawCard(Item& it) {
        SDL_Renderer* r = it.r;
        int w = 0, h = 0;
        SDL_SetRenderScale(r, 1, 1);
        SDL_GetCurrentRenderOutputSize(r, &w, &h);
        if (it.target)
            SDL_SetRenderDrawColor(r, 28, 98, 190, 255);
        else
            SDL_SetRenderDrawColor(r, 24, 26, 30, 255);
        SDL_RenderClear(r);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        for (int k = 0; k < 4; ++k) {
            SDL_FRect border{float(k), float(k), float(w - 2 * k), float(h - 2 * k)};
            SDL_RenderRect(r, &border);
        }
        const std::string num = std::to_string(it.index + 1);
        const float glyph = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
        const float big = std::floor(h * 0.42f / glyph);
        SDL_SetRenderScale(r, big, big);
        SDL_RenderDebugText(r, (w / big - glyph * num.size()) / 2, h * 0.08f / big, num.c_str());
        const float small = std::max(1.0f, std::floor(h / 140.0f));
        SDL_SetRenderScale(r, small, small);
        auto line = [&](std::string t, float y) {
            const size_t maxChars = size_t(std::max(4.0f, (w / small - 16) / glyph));
            if (t.size() > maxChars) t = t.substr(0, maxChars - 2) + "..";
            SDL_RenderDebugText(r, (w / small - glyph * t.size()) / 2, y / small, t.c_str());
        };
        line(it.name, h * 0.56f);
        line(it.res, h * 0.56f + 12 * small);
        line(it.target ? "FeedView's display" : "", h * 0.56f + 24 * small);
        line("Click here or press " + num, h - 22 * small);
        SDL_RenderPresent(r);
    }

    std::vector<Item> items_;
    int skipIndex_ = -1;
    int targetIndex_ = -1;
    Uint64 until_ = 0;
};

struct CardRect {
    ImVec2 a, b;
    bool contains(ImVec2 p) const { return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y; }
};

// Same card, drawn inside the main window (ImGui) for the screen FeedView already fills.
// Returns the card rectangle so clicks on it can be detected.
CardRect identifyCardInWindow(ImVec2 view, float uiScale, int number, const DisplayInfo& d, bool isTarget) {
    const float w = std::min(view.x * 0.75f, 480.0f * uiScale), h = w * 5 / 8;
    const ImVec2 a((view.x - w) * 0.5f, (view.y - h) * 0.5f), b(a.x + w, a.y + h);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(a, b, isTarget ? IM_COL32(28, 98, 190, 255) : IM_COL32(24, 26, 30, 255), 8.0f);
    dl->AddRect(a, b, IM_COL32(255, 255, 255, 255), 8.0f, 0, 4.0f);
    const std::string num = std::to_string(number);
    centeredText(dl, ImVec2(view.x * 0.5f, a.y + h * 0.3f), num.c_str(), h * 0.42f / ImGui::GetFontSize(),
                 IM_COL32(255, 255, 255, 255));
    const std::string res = IdentifyOverlay::resolution(d);
    centeredText(dl, ImVec2(view.x * 0.5f, a.y + h * 0.62f), d.name.c_str(), 1.2f, IM_COL32(255, 255, 255, 255));
    centeredText(dl, ImVec2(view.x * 0.5f, a.y + h * 0.62f + ImGui::GetFontSize() * 1.5f), res.c_str(), 1.0f,
                 IM_COL32(220, 220, 220, 255));
    const std::string hint = "Click here or press " + num;
    centeredText(dl, ImVec2(view.x * 0.5f, b.y - ImGui::GetFontSize() * 1.4f), hint.c_str(), 1.0f,
                 IM_COL32(220, 220, 220, 255));
    return CardRect{a, b};
}

// ---------------------------------------------------------------------------------------
// Short on-screen notices ("Display 2 is back - fullscreen restored")

struct Toast {
    std::string text;
    Uint64 until;
};

void drawToasts(std::vector<Toast>& toasts, Uint64 now, ImVec2 view, float top, float uiScale) {
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const Toast& t) { return now > t.until; }),
                 toasts.end());
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float pad = 10.0f * uiScale;
    float y = top;
    for (const auto& t : toasts) {
        ImVec2 ts = ImGui::CalcTextSize(t.text.c_str(), nullptr, false, view.x - 6 * pad);
        ImVec2 a(std::floor((view.x - ts.x) * 0.5f - pad), y);
        ImVec2 b(a.x + ts.x + 2 * pad, a.y + ts.y + 2 * pad * 0.7f);
        dl->AddRectFilled(a, b, IM_COL32(20, 22, 26, 235), 6.0f * uiScale);
        dl->AddRect(a, b, IM_COL32(90, 150, 230, 255), 6.0f * uiScale, 0, 1.5f * uiScale);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + pad, a.y + pad * 0.7f),
                    IM_COL32(240, 240, 240, 255), t.text.c_str(), nullptr, view.x - 6 * pad);
        y = b.y + pad * 0.6f;
    }
}

// ---------------------------------------------------------------------------------------
// QR code for the web remote's address (drawn with ImGui; white quiet zone around it)

void drawQrCode(const std::string& text, float size) {
    try {
        const auto qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int n = qr.getSize(), quiet = 2;
        const float cell = std::max(1.0f, std::floor(size / float(n + 2 * quiet)));
        const float total = cell * float(n + 2 * quiet);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + total, p.y + total), IM_COL32(255, 255, 255, 255));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (qr.getModule(x, y)) {
                    const ImVec2 a(p.x + (x + quiet) * cell, p.y + (y + quiet) * cell);
                    dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(0, 0, 0, 255));
                }
        ImGui::Dummy(ImVec2(total, total));
    } catch (...) {
        ImGui::TextDisabled("(address too long for a QR code)");
    }
}

// "1", "true", "on" / "0", "false", "off" / "toggle" (or missing) -> new value.
std::optional<bool> parseSwitch(const RemoteCommand& c, const char* key, bool current) {
    const std::string v = c.param(key, "toggle");
    if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    if (v == "toggle") return !current;
    return std::nullopt;
}

int64_t unixMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int listSourcesAndExit(NdiRuntime& runtime, const Options& opt, const Settings& settings) {
    if (!runtime.loaded()) {
        std::fprintf(stderr, "NDI runtime not found. %s\nDownload: %s\n", runtime.error().c_str(),
                     NdiRuntime::downloadUrl());
        return 2;
    }
    SourceFinder finder(runtime.api());
    finder.start(opt.extraIpsSet ? opt.extraIps : settings.extraIps);
    SDL_Delay(Uint32(opt.listSeconds * 1000));
    auto list = finder.sources();
    finder.stop();
    for (const auto& s : list) std::printf("%s\n", s.name.c_str());
    std::fflush(stdout);
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    if (opt.help || opt.version || opt.listSources || !opt.error.empty()) attachConsoleIfAny();
    if (!opt.error.empty()) {
        std::fprintf(stderr, "%s\n\n%s", opt.error.c_str(), kUsage);
        return 1;
    }
    if (opt.help) {
        std::printf("%s", kUsage);
        return 0;
    }
    if (opt.version) {
        std::printf("FeedView %s\n", FEEDVIEW_VERSION);
        return 0;
    }

    SDL_SetAppMetadata(kAppName, FEEDVIEW_VERSION, "app.feedview.player");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    // macOS: plain (non-Spaces) fullscreen switches instantly and stays on its screen, and
    // a click on an inactive window acts immediately - both matter during a live event.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    // ---- Settings
    std::string settingsPath;
    if (char* pref = SDL_GetPrefPath("FeedView", "FeedView")) {
        settingsPath = std::string(pref) + "settings.ini";
        SDL_free(pref);
    }
    Settings settings;
    if (!settingsPath.empty()) settings.load(settingsPath);
    Settings saved = settings;  // command-line choices below are remembered like UI changes
    if (opt.sourceSet) settings.source = opt.source;
    if (opt.extraIpsSet) settings.extraIps = opt.extraIps;
    if (opt.volume >= 0) settings.volume = opt.volume;
    if (opt.mute) settings.muted = true;
    if (settings.remotePin.empty()) settings.remotePin = RemoteServer::generatePin();

    // ---- NDI runtime
    const char* base = SDL_GetBasePath();
    const std::string appDir = base ? base : "";
    NdiRuntime runtime;
    runtime.load(appDir);

    if (opt.listSources) return listSourcesAndExit(runtime, opt, settings);

    // ---- SDL
    bool haveAudio = true;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        haveAudio = false;
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppName, SDL_GetError(), nullptr);
            return 1;
        }
    }

    float uiScale = contentScaleFor(nullptr);
    SDL_Window* window = SDL_CreateWindow(kAppName, int(960 * uiScale), int(540 * uiScale),
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppName, SDL_GetError(), nullptr);
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 320, 180);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppName, SDL_GetError(), window);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    const SDL_WindowID mainWindowId = SDL_GetWindowID(window);

    // ---- ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    applyUiStyle(uiScale);
    ImGuiStyle& style = ImGui::GetStyle();
    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kUiFontData), int(kUiFontDataSize), 16.0f, &cfg);
    }
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // ---- Audio + NDI
    AudioOut audio;
    if (haveAudio) haveAudio = audio.open();
    audio.setGain(volumeToGain(settings.volume, settings.muted));

    std::unique_ptr<SourceFinder> finder;
    std::unique_ptr<Receiver> receiver;
    auto startNdi = [&]() {
        finder = std::make_unique<SourceFinder>(runtime.api());
        finder->start(settings.extraIps);
        receiver = std::make_unique<Receiver>(runtime.api(), [&audio](const float* s, int frames, int rate) {
            audio.push(s, frames, rate);
        });
        receiver->setAudioPair(settings.audioPair);
    };
    if (runtime.loaded()) startNdi();

    VideoFrame frame;  // frame currently shown
    SDL_Texture* texture = nullptr;
    int texW = 0, texH = 0;

    std::vector<NdiSource> sources;
    Uint64 lastConnect = 0;
    auto urlFor = [&](const std::string& name) -> std::string {
        for (const auto& s : sources)
            if (s.name == name) return s.url;
        return std::string();
    };
    auto selectSource = [&](const std::string& name) {
        if (!receiver) return;
        receiver->connect(name, urlFor(name));  // stops the old capture thread first
        audio.flush();
        lastConnect = SDL_GetTicks();
        frame = VideoFrame{};
        settings.source = name;
        std::string title = name.empty() ? std::string(kAppName) : name + " - " + kAppName;
        SDL_SetWindowTitle(window, title.c_str());
    };
    if (receiver && !settings.source.empty()) selectSource(settings.source);

    // ---- Displays / fullscreen (see display_manager.h for the behaviour)
    std::vector<Toast> toasts;
    std::deque<RemoteNotice> notices;  // shown in the web remote
    uint64_t nextNoticeId = 1;
    // True while handling the operator's own keyboard/mouse input. Messages caused by
    // anything else (display changes, the web remote) are not drawn over the fullscreen
    // picture - the audience would see them - but they are always listed in the web remote.
    bool localAction = false;
    auto notify = [&](const std::string& m) {
        SDL_Log("%s", m.c_str());
        notices.push_back({nextNoticeId++, unixMillis(), m});
        if (notices.size() > 30) notices.pop_front();
        const bool fullscreenNow = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
        if (localAction || !fullscreenNow) {
            toasts.push_back({m, SDL_GetTicks() + 6000});
            if (toasts.size() > 3) toasts.erase(toasts.begin());
        }
    };
    SdlDisplayPlatform platform(window, notify);
    DisplayManager dm(platform);
    dm.setTarget({settings.displayName, settings.displayNth, settings.displayIndex});
    if (opt.display >= 0) dm.setTarget({std::string(), 0, opt.display});  // --display N
    IdentifyOverlay identify;

    auto toggleIdentify = [&]() {
        if (identify.active()) {
            identify.hide();
            return;
        }
        if (int(dm.displays().size()) < FEEDVIEW_IDENTIFY_MIN_DISPLAYS) {
            notify("Only one display is connected");
            return;
        }
        const SDL_DisplayID here = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? SDL_GetDisplayForWindow(window) : 0;
        identify.show(dm.displays(), dm.targetIndex(), here, SDL_GetTicks());
        notify("Click a screen's card or press its number to show FeedView there (Esc cancels)");
    };

    SDL_ShowWindow(window);
    dm.start(SDL_GetTicks(), opt.fullscreen || settings.startFullscreen);
    if (dm.target().name.empty() && dm.targetIndex() >= 0) {
        // --display N or an older settings file: from now on remember it by name.
        const int ti = dm.targetIndex();
        const DisplayInfo& d = dm.displays()[ti];
        dm.setTarget({d.name, d.nth, ti});
    }
    uiScale = contentScaleFor(window);
    applyUiStyle(uiScale);
    bool rescaleUi = false;

    // ---- Web remote (see remote_server.h)
    const std::string hostName = localHostName();
    const Uint64 startTicks = SDL_GetTicks();
    RemoteServer remote;
    remote.setPage(std::string(reinterpret_cast<const char*>(kWebPage), kWebPageSize));
    remote.setFont(kUiFontData, kUiFontDataSize);
    remote.setIdentity(hostName, FEEDVIEW_VERSION);
    remote.setActions({"fullscreen", "display", "identify", "source", "reconnect", "volume", "mute", "audio-pair",
                       "settings"});
    std::string remoteError;
    std::vector<std::string> localAddresses = localIPv4Addresses();
    Uint64 lastAddressRefresh = SDL_GetTicks();
    auto applyRemoteSettings = [&]() {
        remote.setAuth(settings.remotePin, settings.remoteRequirePin);
        const bool want = settings.remoteEnabled && !opt.noRemote;
        if (want && !remote.running()) {
            const int port = opt.remotePort > 0 ? opt.remotePort : settings.remotePort;
            if (remote.start(port) == 0) {
                remoteError = "Ports " + std::to_string(port) + "-" + std::to_string(port + 9) +
                              " are all in use by other programs. Pick another port with --remote-port.";
                SDL_Log("%s", remoteError.c_str());
            } else {
                remoteError.clear();
                SDL_Log("Web remote on port %d", remote.port());
            }
        } else if (!want && remote.running()) {
            remote.stop();
        }
    };
    applyRemoteSettings();
    auto remoteUrls = [&]() {
        std::vector<std::string> urls;
        if (!remote.running()) return urls;
        const std::string port = ":" + std::to_string(remote.port());
        for (const auto& a : localAddresses) urls.push_back("http://" + a + port);
        urls.push_back("http://" + hostName + ".local" + port);
        return urls;
    };
    // Latest values from the frame loop, for the web remote (also valid while minimized).
    ReceiverStatus lastStatus;
    bool lastHasPicture = false, lastSignalLost = false;

    auto remoteState = [&]() {
        RemoteSnapshot r;
        r.host = hostName;
        r.version = FEEDVIEW_VERSION;
        r.runtimeLoaded = runtime.loaded();
        r.runtimeVersion = runtime.versionString();
        r.uptimeSeconds = double(SDL_GetTicks() - startTicks) / 1000.0;
        r.source = receiver ? receiver->source() : settings.source;
        for (const auto& src : sources) {
            r.sources.push_back(src.name);
            if (src.name == r.source) r.sourceListed = true;
        }
        r.status = lastStatus;
        r.hasPicture = lastHasPicture;
        r.signalLost = lastSignalLost;
        r.frameSerial = frame.serial;
        r.volume = settings.volume;
        r.muted = settings.muted;
        r.audioPair = settings.audioPair;
        r.haveAudioDevice = haveAudio;
        r.bufferMs = audio.bufferMs();
        const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
        for (const auto& d : dm.displays()) {
            RemoteDisplay rd;
            rd.name = d.name;
            rd.x = d.bounds.x;
            rd.y = d.bounds.y;
            rd.w = d.bounds.w;
            rd.h = d.bounds.h;
            if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(d.id)) rd.refreshHz = m->refresh_rate;
            rd.scale = SDL_GetDisplayContentScale(d.id);
            rd.primary = d.id == primary;
            r.displays.push_back(rd);
        }
        r.targetIndex = dm.targetIndex();
        r.targetName = dm.target().name;
        r.targetLabel = dm.targetLabel();
        r.wantFullscreen = dm.wantFullscreen();
        r.fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
        r.waiting = dm.waitingForTarget();
        r.identify = identify.active();
        SDL_GetWindowPosition(window, &r.windowX, &r.windowY);
        SDL_GetWindowSize(window, &r.windowW, &r.windowH);
        const SDL_DisplayID wd = SDL_GetDisplayForWindow(window);
        for (int i = 0; i < int(dm.displays().size()); ++i)
            if (dm.displays()[i].id == wd) r.windowDisplay = i;
        r.startFullscreen = settings.startFullscreen;
        r.showInfo = settings.showInfo;
        r.cleanOutput = settings.cleanOutput;
        r.extraIps = settings.extraIps;
        r.pinRequired = settings.remoteRequirePin;
        r.urls = remoteUrls();
        r.clients = remote.recentClients(15);
        r.notices.assign(notices.begin(), notices.end());
        return remoteStateJson(r);
    };

    char extraIpsBuf[512] = {};
    SDL_strlcpy(extraIpsBuf, settings.extraIps.c_str(), sizeof extraIpsBuf);

    // Runs one web-remote command on this (the UI) thread.
    auto handleRemote = [&](const RemoteCommand& c) -> RemoteResult {
        const Uint64 t = SDL_GetTicks();
        const std::string& a = c.action;
        if (a == "fullscreen") {
            auto on = parseSwitch(c, "on", dm.wantFullscreen() && !dm.waitingForTarget());
            if (!on) return RemoteResult::fail("on must be 1, 0 or toggle");
            dm.setFullscreen(*on, t);
            return {true, *on ? "Fullscreen on " + dm.targetLabel() : "Fullscreen off"};
        }
        if (a == "display") {
            const int n = std::atoi(c.param("number").c_str());
            const int count = int(dm.displays().size());
            if (n < 1 || n > count)
                return RemoteResult::fail("There is no display " + c.param("number") + " (1-" + std::to_string(count) + ")");
            const bool fs = parseSwitch(c, "fullscreen", true).value_or(true);
            identify.hide();
            dm.chooseDisplay(n - 1, fs, t);
            return {true, (fs ? "Fullscreen on " : "Moved to ") + dm.label(n - 1)};
        }
        if (a == "identify") {
            auto on = parseSwitch(c, "on", identify.active());
            if (!on) return RemoteResult::fail("on must be 1, 0 or toggle");
            if (*on == identify.active()) return {true, *on ? "Display numbers are showing" : "Display numbers hidden"};
            if (*on && int(dm.displays().size()) < FEEDVIEW_IDENTIFY_MIN_DISPLAYS)
                return RemoteResult::fail("Only one display is connected", 409);
            toggleIdentify();
            return {true, *on ? "Showing display numbers on all screens" : "Display numbers hidden"};
        }
        if (a == "source") {
            if (!c.has("name")) return RemoteResult::fail("name is missing (empty name = no source)");
            if (!receiver) return RemoteResult::fail("The NDI runtime is not loaded", 409);
            const std::string name = c.param("name");
            selectSource(name);
            return {true, name.empty() ? std::string("No source") : "Showing " + name};
        }
        if (a == "reconnect") {
            if (!receiver || receiver->source().empty()) return RemoteResult::fail("No source selected", 409);
            const std::string name = receiver->source();
            selectSource(name);
            return {true, "Reconnecting to " + name};
        }
        if (a == "volume") {
            const std::string v = c.param("value");
            if (v.empty() || v.find_first_not_of("+-0123456789") != std::string::npos)
                return RemoteResult::fail("value must be 0-100, or +N / -N to step");
            const int n = std::atoi(v.c_str());
            settings.volume = std::clamp((v[0] == '+' || v[0] == '-') ? settings.volume + n : n, 0, 100);
            return {true, "Volume " + std::to_string(settings.volume) + "%"};
        }
        if (a == "mute") {
            auto on = parseSwitch(c, "on", settings.muted);
            if (!on) return RemoteResult::fail("on must be 1, 0 or toggle");
            settings.muted = *on;
            return {true, *on ? "Muted" : "Unmuted"};
        }
        if (a == "audio-pair") {
            const int first = std::atoi(c.param("first").c_str());  // 1, 3, 5 ...
            if (first < 1 || first > 15 || first % 2 == 0)
                return RemoteResult::fail("first must be an odd channel number: 1, 3, 5 ...");
            settings.audioPair = first - 1;
            return {true, "Audio channels " + std::to_string(first) + "-" + std::to_string(first + 1)};
        }
        if (a == "settings") {
            for (const char* k : {"start_fullscreen", "show_info", "clean_output"}) {
                if (!c.has(k)) continue;
                bool* field = std::string(k) == "start_fullscreen" ? &settings.startFullscreen
                              : std::string(k) == "show_info"      ? &settings.showInfo
                                                                   : &settings.cleanOutput;
                auto v = parseSwitch(c, k, *field);
                if (!v) return RemoteResult::fail(std::string(k) + " must be 1, 0 or toggle");
                *field = *v;
            }
            if (c.has("extra_ips")) {
                const std::string ips = c.param("extra_ips");
                if (ips.size() >= sizeof extraIpsBuf) return RemoteResult::fail("extra_ips is too long");
                if (ips != settings.extraIps) {
                    settings.extraIps = ips;
                    SDL_strlcpy(extraIpsBuf, ips.c_str(), sizeof extraIpsBuf);
                    if (finder) finder->start(settings.extraIps);
                }
            }
            return {true, "Settings saved"};
        }
        return RemoteResult::fail("Unknown command", 404);
    };
    Uint64 lastStatePublish = 0, lastPreviewTicks = 0;
    uint64_t lastPreviewSerial = 0;
    bool previewPublished = false;

    // ---- Main loop
    bool running = true;
    Uint64 lastActivity = SDL_GetTicks();
    Uint64 lastSaveCheck = 0;
    Uint64 lastFrameTicks = SDL_GetTicksNS();
    bool cursorVisible = true;

    while (running) {
        if (finder) sources = finder->sources();

        // If the selected source shows up after we connected (common at startup, and the only
        // way to reach sources found via extra IPs) or comes back on a new address after its
        // sender restarted, reconnect to that address. A working connection is never touched.
        if (receiver && !receiver->source().empty() && SDL_GetTicks() - lastConnect > 2000) {
            const std::string url = urlFor(receiver->source());
            if (!url.empty() && url != receiver->url() && !receiver->status().connected) {
                receiver->connect(receiver->source(), url);
                audio.flush();
                lastConnect = SDL_GetTicks();
            }
        }

        SDL_Event e;
        bool closePopups = false;
        localAction = true;  // notices from here on are answers to the operator's own input
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (e.window.windowID == SDL_GetWindowID(window)) running = false;
                    break;
                case SDL_EVENT_DISPLAY_ADDED:
                case SDL_EVENT_DISPLAY_REMOVED:
                case SDL_EVENT_DISPLAY_MOVED:
                case SDL_EVENT_DISPLAY_ORIENTATION:
                case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
                case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
                    dm.displaysChanged(SDL_GetTicks());
                    identify.hide();  // its cards may now sit on the wrong screens
                    rescaleUi = true;
                    break;
                case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
                    rescaleUi = true;
                    break;
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                    if (e.window.windowID == mainWindowId) {
                        dm.windowDisplayChanged(SDL_GetTicks());
                        rescaleUi = true;
                    }
                    break;
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    if (e.window.windowID == mainWindowId) rescaleUi = true;
                    break;
                case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                    if (e.window.windowID == mainWindowId) dm.windowLeftFullscreen(SDL_GetTicks());
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                case SDL_EVENT_MOUSE_WHEEL:
                    lastActivity = SDL_GetTicks();
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    lastActivity = SDL_GetTicks();
                    if (e.button.windowID != mainWindowId) {
                        const int idx = identify.hitTest(e.button.windowID);
                        if (idx >= 0) {
                            identify.hide();
                            dm.chooseDisplay(idx, true, SDL_GetTicks());
                        }
                    } else if (e.button.button == SDL_BUTTON_LEFT && e.button.clicks == 2 && !io.WantCaptureMouse &&
                               !identify.active()) {
                        dm.toggleFullscreen(SDL_GetTicks());
                    }
                    break;
                case SDL_EVENT_KEY_DOWN: {
                    const bool volumeKey = e.key.key == SDLK_UP || e.key.key == SDLK_DOWN;
                    if (io.WantTextInput || (e.key.repeat && !volumeKey)) break;
                    const SDL_Keycode k = e.key.key;
                    const SDL_Scancode sc = e.key.scancode;
                    const bool cmd = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
                    const Uint64 t = SDL_GetTicks();
                    // Digits by physical key, so they work the same on every keyboard layout.
                    int digit = -1;
                    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) digit = int(sc - SDL_SCANCODE_1) + 1;
                    else if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) digit = int(sc - SDL_SCANCODE_KP_1) + 1;
                    else if (sc == SDL_SCANCODE_0 || sc == SDL_SCANCODE_KP_0) digit = 0;

                    if (digit >= 1 && (cmd || identify.active())) {
                        identify.hide();  // Ctrl/Cmd+N, or N while the display numbers are shown
                        dm.chooseDisplay(digit - 1, true, t);
                    } else if (digit == 0) {
                        if (identify.active()) identify.hide();
                        else if (!cmd) selectSource("");
                    } else if (digit >= 1) {
                        if (size_t(digit - 1) < sources.size()) selectSource(sources[digit - 1].name);
                    } else if (k == SDLK_D) {
                        toggleIdentify();
                    } else if (k == SDLK_F || k == SDLK_F11) {
                        dm.toggleFullscreen(t);
                    } else if (k == SDLK_ESCAPE) {
                        // Esc closes whatever is open first, and only then leaves fullscreen.
                        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                            closePopups = true;
                        else if (identify.active()) identify.hide();
                        else if (dm.wantFullscreen()) dm.setFullscreen(false, t);
                    } else if (k == SDLK_M) {
                        settings.muted = !settings.muted;
                        lastActivity = SDL_GetTicks();
                    } else if (k == SDLK_UP || k == SDLK_DOWN) {
                        settings.volume = std::clamp(settings.volume + (k == SDLK_UP ? 5 : -5), 0, 100);
                        settings.muted = false;
                        lastActivity = SDL_GetTicks();
                    } else if (k == SDLK_I) {
                        settings.showInfo = !settings.showInfo;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        localAction = false;
        if (!running) break;

        // ---- Keep the window on the right screen; follow DPI when it moves between monitors
        {
            const Uint64 t = SDL_GetTicks();
            dm.update(t);
            identify.update(t);
            identify.render();
            const DisplayTarget& dt = dm.target();
            settings.displayName = dt.name;
            settings.displayNth = dt.nth;
            settings.displayIndex = dt.index;
            if (rescaleUi) {
                rescaleUi = false;
                const float s = contentScaleFor(window);
                if (std::fabs(s - uiScale) > 0.01f) {
                    uiScale = s;
                    applyUiStyle(uiScale);
                }
            }
        }

        // ---- Web remote: run queued commands, publish state and preview frames
        {
            const Uint64 t = SDL_GetTicks();
            if (t - lastAddressRefresh > 5000) {  // the network can change during a show
                localAddresses = localIPv4Addresses();
                lastAddressRefresh = t;
            }
            if (remote.running()) {
                const int handled = remote.processCommands(handleRemote, remoteState);
                if (handled) {
                    lastStatePublish = t;
                } else if (t - lastStatePublish >= 250) {
                    remote.publishState(remoteState());
                    lastStatePublish = t;
                }
                if (!lastHasPicture) {
                    if (previewPublished) remote.clearPreview();
                    previewPublished = false;
                } else if (remote.previewWanted() && frame.serial != lastPreviewSerial && t - lastPreviewTicks >= 200) {
                    int pw = 0, ph = 0;
                    std::vector<uint8_t> rgb = makePreview(frame, 640, pw, ph);
                    if (!rgb.empty()) {
                        remote.publishPreview(std::move(rgb), pw, ph, frame.serial);
                        previewPublished = true;
                    }
                    lastPreviewSerial = frame.serial;
                    lastPreviewTicks = t;
                }
            }
        }

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(20);
            continue;
        }

        // ---- New video frame -> texture
        if (receiver && receiver->fetchVideo(frame)) {
            if (!texture || texW != frame.width || texH != frame.height) {
                if (texture) SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING,
                                            frame.width, frame.height);
                texW = frame.width;
                texH = frame.height;
                if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            }
            if (texture) {
                SDL_SetTextureBlendMode(texture, frame.hasAlpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
                SDL_UpdateTexture(texture, nullptr, frame.pixels.data(), frame.width * 4);
            }
        }
        const ReceiverStatus st = receiver ? receiver->status() : ReceiverStatus{};
        const bool hasPicture = texture && frame.serial != 0;
        // A picture with no fresh frames (or kept across a reconnect) is flagged, never shown as live.
        const bool signalLost = hasPicture && (st.secondsSinceVideo > 1.0 || st.secondsSinceVideo < 0);
        lastStatus = st;
        lastHasPicture = hasPicture;
        lastSignalLost = signalLost;

        audio.setGain(volumeToGain(settings.volume, settings.muted));
        if (receiver && receiver->audioPair() != settings.audioPair) receiver->setAudioPair(settings.audioPair);

        // ---- UI
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        localAction = true;  // clicks in the UI are the operator's own actions

        const Uint64 now = SDL_GetTicks();
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        // The operator is using FeedView's own controls right now.
        const bool localInteraction = popupOpen || identify.active() || io.WantCaptureMouse ||
                                      (now - lastActivity) < Uint64(kOverlayHideSeconds * 1000);
        // Clean output: nothing of FeedView's own is drawn over the fullscreen picture unless
        // the operator is using the controls; status is in the web remote instead.
        const bool quietOutput =
            settings.cleanOutput && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) && !localInteraction;
        const bool showOverlay =
            localInteraction || !runtime.loaded() || (!quietOutput && (!hasPicture || signalLost));
        const ImVec2 view = io.DisplaySize;
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        float barHeight = 0.0f;

        if (!runtime.loaded()) {
            ImGui::SetNextWindowPos(ImVec2(view.x * 0.5f, view.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(view.x - 32.0f, 560.0f * uiScale), 0));
            ImGui::Begin("##noruntime", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushFont(nullptr, style.FontSizeBase * 1.35f);
            ImGui::TextUnformatted("NDI Runtime not found");
            ImGui::PopFont();
            ImGui::Spacing();
#if defined(__linux__)
            ImGui::TextWrapped(
                "FeedView needs libndi.so.6 from the NDI SDK for Linux. Put it in the lib folder next to "
                "FeedView or in /usr/local/lib, make sure avahi-daemon is running, then press Try again.");
            const char* downloadLabel = "Get the NDI SDK";
#else
            ImGui::TextWrapped(
                "FeedView needs the free NDI Runtime, which comes with NDI Tools. Install it, then press "
                "Try again.");
            const char* downloadLabel = "Download NDI Tools";
#endif
            if (!runtime.error().empty()) ImGui::TextDisabled("%s", runtime.error().c_str());
            ImGui::Spacing();
            if (ImGui::Button(downloadLabel)) SDL_OpenURL(NdiRuntime::downloadUrl());
            ImGui::SameLine();
            if (ImGui::Button("Try again") && runtime.load(appDir)) {
                startNdi();
                if (!settings.source.empty()) selectSource(settings.source);
            }
            if (ImGui::CollapsingHeader("Places searched")) {
                for (const auto& p : runtime.triedPaths()) ImGui::TextDisabled("%s", p.c_str());
            }
            ImGui::End();
        } else {
            // Centre messages
            const ImVec2 center(view.x * 0.5f, view.y * 0.5f);
            if (quietOutput) {
                // black, by request
            } else if (receiver && receiver->source().empty()) {
                centeredText(bg, center, "Select an NDI source", 1.6f, IM_COL32(230, 230, 230, 255));
                centeredText(bg, ImVec2(center.x, center.y + ImGui::GetFontSize() * 2.0f),
                             sources.empty() ? "Searching the network..." : "Use the Source menu above, or press 1-9",
                             1.0f, IM_COL32(150, 150, 150, 255));
                const auto urls = remoteUrls();
                if (!urls.empty()) {
                    const std::string line = "Web remote: " + urls.front();
                    centeredText(bg, ImVec2(center.x, view.y - ImGui::GetFontSize() * 2.0f), line.c_str(), 1.0f,
                                 IM_COL32(120, 120, 120, 255));
                }
            } else if (!hasPicture) {
                std::string msg = st.connected ? "Waiting for video from " : "Connecting to ";
                msg += receiver->source();
                centeredText(bg, center, msg.c_str(), 1.3f, IM_COL32(220, 220, 220, 255));
            }

            if (showOverlay) {
                // ---- Top bar
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(view.x, 0));
                ImGui::Begin("##bar", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

                // Source picker
                const std::string& cur = receiver->source();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Source");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(std::clamp(view.x * 0.38f, 180.0f * uiScale, 440.0f * uiScale));
                if (ImGui::BeginCombo("##source", cur.empty() ? "None" : cur.c_str(), ImGuiComboFlags_HeightLarge)) {
                    if (closePopups) ImGui::CloseCurrentPopup();
                    if (ImGui::Selectable("    None", cur.empty())) selectSource("");
                    for (size_t i = 0; i < sources.size(); ++i) {
                        char label[600];
                        if (i < 9)
                            std::snprintf(label, sizeof label, "%zu   %s", i + 1, sources[i].name.c_str());
                        else
                            std::snprintf(label, sizeof label, "    %s", sources[i].name.c_str());
                        bool selected = sources[i].name == cur;
                        if (ImGui::Selectable(label, selected)) selectSource(sources[i].name);
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    if (!cur.empty() && urlFor(cur).empty() &&
                        std::none_of(sources.begin(), sources.end(), [&](const NdiSource& s) { return s.name == cur; })) {
                        ImGui::Separator();
                        ImGui::TextDisabled("    %s (offline)", cur.c_str());
                    }
                    if (sources.empty()) ImGui::TextDisabled("    Searching the network...");
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%zu found", sources.size());

                // Audio
                sameLineIfFits(330.0f * uiScale);
                ImGui::TextUnformatted("Audio");
                ImGui::SameLine();
                const int chans = st.audioChannels;
                ImGui::SetNextItemWidth(92.0f * uiScale);
                if (ImGui::BeginCombo("##pair", pairLabel(settings.audioPair, chans).c_str())) {
                    if (closePopups) ImGui::CloseCurrentPopup();
                    int pairs = std::max(1, (chans + 1) / 2);
                    for (int p = 0; p < pairs; ++p) {
                        if (ImGui::Selectable(pairLabel(p * 2, chans).c_str(), settings.audioPair == p * 2))
                            settings.audioPair = p * 2;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::Checkbox("Mute", &settings.muted);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f * uiScale);
                ImGui::SliderInt("##volume", &settings.volume, 0, 100, "%d%%");

                // Fullscreen + display choice
                const auto& screens = dm.displays();
                const int ti = dm.targetIndex();
                const bool showScreens = screens.size() > 1 || (ti < 0 && !dm.target().name.empty());
                sameLineIfFits((showScreens ? 470.0f : 200.0f) * uiScale);
                const char* fsLabel = dm.waitingForTarget() ? "Fullscreen here"
                                      : dm.wantFullscreen()    ? "Exit fullscreen"
                                                               : "Fullscreen";
                if (ImGui::Button(fsLabel)) dm.toggleFullscreen(now);
                if (showScreens && !screens.empty()) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(230.0f * uiScale);
                    if (ImGui::BeginCombo("##display", dm.targetLabel().c_str(), ImGuiComboFlags_HeightLarge)) {
                    if (closePopups) ImGui::CloseCurrentPopup();
                        for (int i = 0; i < int(screens.size()); ++i)
                            if (ImGui::Selectable(dm.label(i).c_str(), i == ti)) dm.chooseDisplay(i, false, now);
                        if (ti < 0 && !dm.target().name.empty()) {
                            ImGui::Separator();
                            ImGui::TextDisabled("%s", dm.targetLabel().c_str());
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Identify")) toggleIdentify();
                }
                if (dm.waitingForTarget()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Waiting for %s", dm.target().name.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("Settings")) ImGui::OpenPopup("settings");
                if (ImGui::BeginPopup("settings")) {
                    if (closePopups) ImGui::CloseCurrentPopup();
                    ImGui::TextUnformatted("Extra discovery IPs");
                    ImGui::TextDisabled("For sources on other subnets/VLANs, e.g. 10.0.1.20,10.0.2.0");
                    ImGui::SetNextItemWidth(300.0f * uiScale);
                    bool apply = ImGui::InputText("##ips", extraIpsBuf, sizeof extraIpsBuf,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine();
                    apply |= ImGui::Button("Apply");
                    if (apply && finder && settings.extraIps != extraIpsBuf) {
                        settings.extraIps = extraIpsBuf;
                        finder->start(settings.extraIps);
                    }
                    ImGui::Separator();
                    ImGui::Checkbox("Start in fullscreen", &settings.startFullscreen);
                    ImGui::Checkbox("Always show info (I)", &settings.showInfo);
                    ImGui::Checkbox("Clean output", &settings.cleanOutput);
                    ImGui::SameLine();
                    ImGui::TextDisabled("no messages over the fullscreen picture");
                    ImGui::Separator();
                    ImGui::TextDisabled("Keys: F fullscreen, Esc leave, 1-9 source, 0 none,");
                    ImGui::TextDisabled("D identify displays, Ctrl+1-9 fullscreen on display N,");
                    ImGui::TextDisabled("M mute, Up/Down volume, I info. Double-click = fullscreen.");
                    ImGui::Separator();
                    ImGui::TextDisabled("FeedView %s", FEEDVIEW_VERSION);
                    ImGui::TextDisabled("Runtime: %s", runtime.versionString().c_str());
                    ImGui::TextDisabled("NDI(R) is a registered trademark of Vizrt NDI AB.");
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                const auto clients = remote.recentClients(15);
                if (ImGui::Button(clients.empty() ? "Remote" : "Remote (in use)")) {
                    localAddresses = localIPv4Addresses();
                    ImGui::OpenPopup("remote");
                }
                if (ImGui::BeginPopup("remote")) {
                    if (closePopups) ImGui::CloseCurrentPopup();
                    ImGui::PushFont(nullptr, style.FontSizeBase * 1.25f);
                    ImGui::TextUnformatted("Web remote");
                    ImGui::PopFont();
                    const auto urls = remoteUrls();
                    if (!remote.running()) {
                        ImGui::TextDisabled("%s", remoteError.empty() ? "The web remote is off." : remoteError.c_str());
                    } else if (localAddresses.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                           "This computer has no network connection right now.");
                    } else {
                        const std::string qrText =
                            urls.front() + (settings.remoteRequirePin ? "/#pin=" + settings.remotePin : "/");
                        drawQrCode(qrText, 190.0f * uiScale);
                        ImGui::SameLine();
                        ImGui::BeginGroup();
                        ImGui::TextUnformatted("Scan the code, or open this on a phone or");
                        ImGui::TextUnformatted("computer on the same network:");
                        ImGui::Spacing();
                        for (const auto& u : urls) ImGui::TextUnformatted(u.c_str());
                        if (settings.remoteRequirePin) {
                            ImGui::Spacing();
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("PIN");
                            ImGui::SameLine();
                            ImGui::PushFont(nullptr, style.FontSizeBase * 1.6f);
                            ImGui::TextUnformatted(settings.remotePin.c_str());
                            ImGui::PopFont();
                        }
                        ImGui::Spacing();
                        if (clients.empty()) {
                            ImGui::TextDisabled("Nobody is using it right now.");
                        } else {
                            std::string who;
                            for (const auto& c : clients) who += (who.empty() ? "" : ", ") + c;
                            ImGui::Text("In use from %s", who.c_str());
                        }
                        ImGui::EndGroup();
                    }
                    ImGui::Separator();
                    bool enabled = settings.remoteEnabled;
                    if (ImGui::Checkbox("Web remote on", &enabled)) {
                        settings.remoteEnabled = enabled;
                        applyRemoteSettings();
                    }
                    ImGui::SameLine();
                    bool requirePin = settings.remoteRequirePin;
                    if (ImGui::Checkbox("Require PIN", &requirePin)) {
                        settings.remoteRequirePin = requirePin;
                        applyRemoteSettings();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("New PIN")) {
                        settings.remotePin = RemoteServer::generatePin();
                        applyRemoteSettings();
                    }
                    if (opt.noRemote) ImGui::TextDisabled("Turned off with --no-remote for this run.");
                    if (!settings.remoteRequirePin)
                        ImGui::TextDisabled("Without a PIN anyone on this network can control FeedView.");
                    ImGui::EndPopup();
                }
                barHeight = ImGui::GetWindowHeight();
                ImGui::End();
            }

            // ---- Info / status line
            if ((showOverlay || (settings.showInfo && !quietOutput)) && receiver && !receiver->source().empty()) {
                char info[512];
                std::string fmt = formatRate(st.frameRateN, st.frameRateD);
                int n = 0;
                if (st.width > 0)
                    n = std::snprintf(info, sizeof info, "%dx%d %s  |  %.1f fps", st.width, st.height, fmt.c_str(),
                                      st.measuredFps);
                else
                    n = std::snprintf(info, sizeof info, "No video");
                if (st.audioSampleRate > 0 && n > 0 && n < int(sizeof info)) {
                    // Same fallback as the receiver: a pair the source doesn't have plays ch 1-2.
                    int playing = settings.audioPair < st.audioChannels ? settings.audioPair : 0;
                    n += std::snprintf(info + n, sizeof info - n, "  |  %.1f kHz, %d ch (playing %s)",
                                       st.audioSampleRate / 1000.0, st.audioChannels,
                                       pairLabel(playing, st.audioChannels).c_str());
                    if (haveAudio && n > 0 && n < int(sizeof info))
                        n += std::snprintf(info + n, sizeof info - n, ", buffer %.0f ms", audio.bufferMs());
                }
                if (n > 0 && n < int(sizeof info) && st.droppedVideoFrames > 0)
                    std::snprintf(info + n, sizeof info - n, "  |  dropped %lld", (long long)st.droppedVideoFrames);
                if (!haveAudio && n > 0 && n < int(sizeof info))
                    std::snprintf(info + n, sizeof info - n, "  |  no audio device");

                ImGui::SetNextWindowPos(ImVec2(8.0f * uiScale, view.y - 8.0f * uiScale), ImGuiCond_Always,
                                        ImVec2(0, 1));
                ImGui::SetNextWindowBgAlpha(0.7f);
                ImGui::Begin("##info", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoFocusOnAppearing);
                ImGui::TextUnformatted(info);
                ImGui::End();
            }

            if (signalLost && !quietOutput) {
                std::string msg = (st.connected ? "No video from " : "Source offline: ") + receiver->source();
                centeredText(ImGui::GetForegroundDrawList(), ImVec2(view.x * 0.5f, view.y * 0.5f), msg.c_str(), 1.3f,
                             IM_COL32(255, 90, 80, 255));
            }
        }

        // ---- Identify card for the screen FeedView fills, and notices
        if (identify.skipIndex() >= 0 && identify.skipIndex() < int(dm.displays().size())) {
            const int si = identify.skipIndex();
            const CardRect card =
                identifyCardInWindow(view, uiScale, si + 1, dm.displays()[si], si == dm.targetIndex());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && card.contains(io.MousePos)) {
                identify.hide();
                dm.chooseDisplay(si, true, now);
            }
        }
        if (!quietOutput) drawToasts(toasts, now, view, barHeight + 10.0f * uiScale, uiScale);
        localAction = false;

        // Hide the mouse pointer together with the overlay.
        const bool wantCursor = showOverlay || !runtime.loaded();
        if (wantCursor != cursorVisible) {
            wantCursor ? SDL_ShowCursor() : SDL_HideCursor();
            cursorVisible = wantCursor;
        }

        ImGui::Render();

        // ---- Draw: video first (in output pixels), then the UI on top
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (hasPicture) {
            int ow = 0, oh = 0;
            SDL_GetCurrentRenderOutputSize(renderer, &ow, &oh);
            float aspect = frame.aspect > 0 ? frame.aspect : float(frame.width) / float(frame.height);
            float w = float(ow), h = w / aspect;
            if (h > oh) {
                h = float(oh);
                w = h * aspect;
            }
            SDL_FRect dst{std::floor((ow - w) * 0.5f), std::floor((oh - h) * 0.5f), std::round(w), std::round(h)};
            SDL_RenderTexture(renderer, texture, nullptr, &dst);
            if (signalLost && !quietOutput) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
                SDL_RenderFillRect(renderer, &dst);
            }
        }
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        // If vsync is unavailable (some VMs / remote desktops), don't spin a core at 100%.
        const Uint64 nowNs = SDL_GetTicksNS();
        const Uint64 minFrameNs = SDL_NS_PER_SECOND / 240;
        if (nowNs - lastFrameTicks < minFrameNs) SDL_DelayPrecise(minFrameNs - (nowNs - lastFrameTicks));
        lastFrameTicks = SDL_GetTicksNS();

        // Persist settings shortly after they change.
        if (now - lastSaveCheck > 1000) {
            lastSaveCheck = now;
            if (!(settings == saved) && !settingsPath.empty() && settings.save(settingsPath)) saved = settings;
        }
    }

    if (!(settings == saved) && !settingsPath.empty()) settings.save(settingsPath);

    remote.stop();
    identify.hide();
    receiver.reset();
    finder.reset();
    audio.close();
    if (texture) SDL_DestroyTexture(texture);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    runtime.unload();
    SDL_Quit();
    return 0;
}
