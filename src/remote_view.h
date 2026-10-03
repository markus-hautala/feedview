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

// Where a control is in FeedView's window (pixels), for tests that click it.
struct RemoteUiRect {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;
};

struct RemoteAudioOutput {
    std::string id, name;
    bool isDefault = false;
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
    int frameGapMs = 0;  // longest time between two frames in the last ~5 s (a stall freezes the picture)

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
    bool systemVolume = false;  // volume/mute are the OS's; outputs can be chosen
    std::vector<RemoteAudioOutput> outputs;
    std::string output;  // id of the OS default output

    // Displays (numbered left to right; index 0 = display 1)
    std::vector<RemoteDisplay> displays;
    int targetIndex = -1;  // -1 = chosen display not connected (or none chosen)
    std::string targetName, targetLabel;
    bool wantFullscreen = false, fullscreen = false, waiting = false, identify = false;
    int windowX = 0, windowY = 0, windowW = 0, windowH = 0;
    int windowDisplay = -1;  // index of the display the window is on
    bool onTop = false;            // above other windows right now
    bool controlsVisible = false;  // FeedView's own controls are drawn on its window
    bool cursorVisible = false;    // the mouse pointer is shown over FeedView
    std::string panel;             // open panel/menu: "remote", "settings", "menu" or ""
    bool outputPicture = false;    // a picture is on the output (the new source's or one fading out)
    bool fading = false;           // a fade between sources is running
    bool fadeWaiting = false;      // ...holding the old picture until the new source sends video
    std::string fadeFrom;          // the source fading out
    std::vector<RemoteUiRect> uiRects;  // only when FeedView runs under tests (FEEDVIEW_TEST_UI)

    // Settings
    bool startFullscreen = false, showInfo = false, cleanOutput = false;
    bool alwaysOnTop = true, silenceNotifications = true;
    int fadeMs = 0;
    std::string extraIps;

    // OS notifications
    bool notificationsSupported = false, notificationsPermitted = false, notificationsSilenced = false;
    bool notificationsPermissionPending = false;
    std::string notificationsMessage;

    // Remote
    bool pinRequired = true;
    std::vector<std::string> urls;
    std::vector<std::string> clients;
    std::vector<RemoteNotice> notices;
};

std::string remoteStateJson(const RemoteSnapshot& s);

// The address that opens the remote page (QR code, links in FeedView): "base/", or
// "base/#pin=1234" so the page opens already signed in when a PIN is required.
std::string remoteLink(const std::string& baseUrl, bool pinRequired, const std::string& pin);

// Downscales a frame to at most maxWidth (keeping its display aspect) as packed RGB.
// Transparent areas are composited over black, as FeedView shows them.
std::vector<uint8_t> makePreview(const VideoFrame& f, int maxWidth, int& outW, int& outH);
