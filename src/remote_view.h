// What the web remote shows: a plain snapshot of FeedView's state, its JSON form, and the
// downscaled preview picture. Filled in by main.cpp each time the state is published.
#pragma once

#include "ndi_io.h"

#include <cstdint>
#include <string>
#include <vector>

struct RemoteNotice {
    uint64_t id = 0;
    int64_t unixMs = 0;
    std::string text;
};

struct RemoteDisplay {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;
    float refreshHz = 0;
    float scale = 1;
    bool primary = false;
};

struct RemoteSnapshot {
    // App
    std::string host, version, runtimeVersion;
    bool runtimeLoaded = false;
    double uptimeSeconds = 0;

    // Source
    std::string source;  // empty = none
    bool sourceListed = false;
    std::vector<std::string> sources;
    ReceiverStatus status;
    bool hasPicture = false, signalLost = false;
    uint64_t frameSerial = 0;

    // Audio
    int volume = 100;
    bool muted = false;
    int audioPair = 0;  // first channel, 0-based
    bool haveAudioDevice = false;
    float bufferMs = 0;

    // Displays (numbered left to right; index 0 = display 1)
    std::vector<RemoteDisplay> displays;
    int targetIndex = -1;  // -1 = chosen display not connected (or none chosen)
    std::string targetName, targetLabel;
    bool wantFullscreen = false, fullscreen = false, waiting = false, identify = false;
    int windowX = 0, windowY = 0, windowW = 0, windowH = 0;
    int windowDisplay = -1;  // index of the display the window is on

    // Settings
    bool startFullscreen = false, showInfo = false, cleanOutput = false;
    std::string extraIps;

    // Remote
    bool pinRequired = true;
    std::vector<std::string> urls;
    std::vector<std::string> clients;
    std::vector<RemoteNotice> notices;
};

std::string remoteStateJson(const RemoteSnapshot& s);

// Downscales a frame to at most maxWidth (keeping its display aspect) as packed RGB.
// Transparent areas are composited over black, as FeedView shows them.
std::vector<uint8_t> makePreview(const VideoFrame& f, int maxWidth, int& outW, int& outH);
