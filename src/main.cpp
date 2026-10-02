// FeedView - a small cross-platform player for NDI(R) video sources.
//
// NDI(R) is a registered trademark of Vizrt NDI AB. FeedView is not affiliated with or
// endorsed by Vizrt NDI AB.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "ndi_io.h"
#include "ndi_runtime.h"
#include "settings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
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

namespace {

constexpr const char* kAppName = "FeedView";
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
    std::string error;
};

const char* kUsage =
    "Usage: FeedView [options]\n"
    "\n"
    "  --source NAME       Connect to this NDI source, e.g. \"STUDIO-PC (Program)\"\n"
    "  --fullscreen, -f    Start in fullscreen\n"
    "  --display N         Fullscreen on display N (1 = first display)\n"
    "  --extra-ips LIST    Comma separated IPs to search for sources on other subnets\n"
    "  --volume N          Volume 0-100\n"
    "  --mute              Start muted\n"
    "  --list-sources[=S]  Print the sources found within S seconds (default 3) and exit\n"
    "  --version           Print version and exit\n"
    "  --help              Show this help\n"
    "\n"
    "Keys: F/F11 fullscreen, Esc leave fullscreen, 1-9 pick source, 0 disconnect,\n"
    "      M mute, Up/Down volume, I info overlay.\n";

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
    if (opt.display >= 0) settings.display = opt.display;

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

    const float uiScale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
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

    // ---- ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.10f, 0.86f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.12f, 0.97f);
    style.ScaleAllSizes(uiScale);
    style.FontScaleDpi = uiScale;
    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kUiFontData), int(kUiFontDataSize), 16.0f, &cfg);
        style.FontSizeBase = 16.0f;
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

    // ---- Displays / fullscreen
    std::vector<SDL_DisplayID> displays;
    std::vector<std::string> displayNames;
    auto refreshDisplays = [&]() {
        displays.clear();
        displayNames.clear();
        int n = 0;
        if (SDL_DisplayID* ids = SDL_GetDisplays(&n)) {
            for (int i = 0; i < n; ++i) {
                displays.push_back(ids[i]);
                const char* nm = SDL_GetDisplayName(ids[i]);
                SDL_Rect r{};
                SDL_GetDisplayBounds(ids[i], &r);
                char buf[160];
                std::snprintf(buf, sizeof buf, "%d: %s (%dx%d)", i + 1, nm ? nm : "Display", r.w, r.h);
                displayNames.emplace_back(buf);
            }
            SDL_free(ids);
        }
    };
    refreshDisplays();

    auto isFullscreen = [&]() { return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0; };
    auto setFullscreen = [&](bool on) {
        if (!on) {
            SDL_SetWindowFullscreen(window, false);
            return;
        }
        if (!displays.empty()) {
            int idx = std::clamp(settings.display, 0, int(displays.size()) - 1);
            SDL_DisplayID target = displays[idx];
            if (SDL_GetDisplayForWindow(window) != target) {
                if (isFullscreen()) {
                    SDL_SetWindowFullscreen(window, false);
                    SDL_SyncWindow(window);
                }
                SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(target),
                                      SDL_WINDOWPOS_CENTERED_DISPLAY(target));
                SDL_SyncWindow(window);
            }
        }
        SDL_SetWindowFullscreenMode(window, nullptr);  // borderless "desktop" fullscreen
        SDL_SetWindowFullscreen(window, true);
    };

    SDL_ShowWindow(window);
    if (opt.fullscreen || settings.startFullscreen) setFullscreen(true);

    // ---- Main loop
    bool running = true;
    Uint64 lastActivity = SDL_GetTicks();
    Uint64 lastSaveCheck = 0;
    Uint64 lastFrameTicks = SDL_GetTicksNS();
    bool cursorVisible = true;
    char extraIpsBuf[512] = {};
    SDL_strlcpy(extraIpsBuf, settings.extraIps.c_str(), sizeof extraIpsBuf);

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
                    refreshDisplays();
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                case SDL_EVENT_MOUSE_WHEEL:
                    lastActivity = SDL_GetTicks();
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    lastActivity = SDL_GetTicks();
                    if (e.button.button == SDL_BUTTON_LEFT && e.button.clicks == 2 && !io.WantCaptureMouse)
                        setFullscreen(!isFullscreen());
                    break;
                case SDL_EVENT_KEY_DOWN: {
                    const bool volumeKey = e.key.key == SDLK_UP || e.key.key == SDLK_DOWN;
                    if (io.WantTextInput || (e.key.repeat && !volumeKey)) break;
                    const SDL_Keycode k = e.key.key;
                    if (k == SDLK_F || k == SDLK_F11) {
                        setFullscreen(!isFullscreen());
                    } else if (k == SDLK_ESCAPE) {
                        if (isFullscreen()) setFullscreen(false);
                    } else if (k == SDLK_M) {
                        settings.muted = !settings.muted;
                        lastActivity = SDL_GetTicks();
                    } else if (k == SDLK_UP || k == SDLK_DOWN) {
                        settings.volume = std::clamp(settings.volume + (k == SDLK_UP ? 5 : -5), 0, 100);
                        settings.muted = false;
                        lastActivity = SDL_GetTicks();
                    } else if (k == SDLK_I) {
                        settings.showInfo = !settings.showInfo;
                    } else if (k == SDLK_0) {
                        selectSource("");
                    } else if (k >= SDLK_1 && k <= SDLK_9) {
                        size_t idx = size_t(k - SDLK_1);
                        if (idx < sources.size()) selectSource(sources[idx].name);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        if (!running) break;

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

        audio.setGain(volumeToGain(settings.volume, settings.muted));
        if (receiver && receiver->audioPair() != settings.audioPair) receiver->setAudioPair(settings.audioPair);

        // ---- UI
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const Uint64 now = SDL_GetTicks();
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const bool showOverlay = !runtime.loaded() || !hasPicture || signalLost || popupOpen ||
                                 io.WantCaptureMouse || (now - lastActivity) < Uint64(kOverlayHideSeconds * 1000);
        const ImVec2 view = io.DisplaySize;
        ImDrawList* bg = ImGui::GetBackgroundDrawList();

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
            if (receiver && receiver->source().empty()) {
                centeredText(bg, center, "Select an NDI source", 1.6f, IM_COL32(230, 230, 230, 255));
                centeredText(bg, ImVec2(center.x, center.y + ImGui::GetFontSize() * 2.0f),
                             sources.empty() ? "Searching the network..." : "Use the Source menu above, or press 1-9",
                             1.0f, IM_COL32(150, 150, 150, 255));
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

                // Fullscreen
                sameLineIfFits((displays.size() > 1 ? 330.0f : 200.0f) * uiScale);
                if (ImGui::Button(isFullscreen() ? "Exit fullscreen" : "Fullscreen")) setFullscreen(!isFullscreen());
                if (displays.size() > 1) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(190.0f * uiScale);
                    int d = std::clamp(settings.display, 0, int(displays.size()) - 1);
                    if (ImGui::BeginCombo("##display", displayNames[d].c_str())) {
                        for (int i = 0; i < int(displays.size()); ++i) {
                            if (ImGui::Selectable(displayNames[i].c_str(), i == d)) {
                                settings.display = i;
                                if (isFullscreen()) setFullscreen(true);
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Settings")) ImGui::OpenPopup("settings");
                if (ImGui::BeginPopup("settings")) {
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
                    ImGui::Separator();
                    ImGui::TextDisabled("Keys: F fullscreen, Esc leave, 1-9 source, 0 none,");
                    ImGui::TextDisabled("M mute, Up/Down volume, I info. Double-click = fullscreen.");
                    ImGui::Separator();
                    ImGui::TextDisabled("FeedView %s", FEEDVIEW_VERSION);
                    ImGui::TextDisabled("Runtime: %s", runtime.versionString().c_str());
                    ImGui::TextDisabled("NDI(R) is a registered trademark of Vizrt NDI AB.");
                    ImGui::EndPopup();
                }
                ImGui::End();
            }

            // ---- Info / status line
            if ((showOverlay || settings.showInfo) && receiver && !receiver->source().empty()) {
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

            if (signalLost) {
                std::string msg = (st.connected ? "No video from " : "Source offline: ") + receiver->source();
                centeredText(ImGui::GetForegroundDrawList(), ImVec2(view.x * 0.5f, view.y * 0.5f), msg.c_str(), 1.3f,
                             IM_COL32(255, 90, 80, 255));
            }
        }

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
            if (signalLost) {
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
