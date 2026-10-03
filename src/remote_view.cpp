#include "remote_view.h"

#include "json_writer.h"

#include <algorithm>
#include <cmath>

std::string remoteStateJson(const RemoteSnapshot& s) {
    JsonWriter j;
    j.beginObject();

    j.key("app").beginObject()
        .field("name", "FeedView")
        .field("version", s.version)
        .field("host", s.host)
        .field("uptime", s.uptimeSeconds)
        .field("frameGapMs", s.frameGapMs)
        .field("runtimeLoaded", s.runtimeLoaded)
        .field("runtimeVersion", s.runtimeVersion)
        .endObject();

    const ReceiverStatus& st = s.status;
    j.key("source").beginObject()
        .field("name", s.source)
        .field("listed", s.sourceListed)
        .field("connected", st.connected)
        .field("hasPicture", s.hasPicture)
        .field("signalLost", s.signalLost)
        .field("frame", s.frameSerial)
        .key("video").beginObject()
        .field("width", st.width)
        .field("height", st.height)
        .field("rateN", st.frameRateN)
        .field("rateD", st.frameRateD)
        .field("fps", st.measuredFps)
        .field("secondsSinceFrame", st.secondsSinceVideo)
        .field("frames", st.videoFrames)
        .field("dropped", st.droppedVideoFrames)
        .endObject()
        .key("audio").beginObject()
        .field("sampleRate", st.audioSampleRate)
        .field("channels", st.audioChannels)
        .endObject()
        .endObject();

    j.key("sources").beginArray();
    for (const auto& n : s.sources) j.value(n);
    j.endArray();

    j.key("audio").beginObject()
        .field("volume", s.volume)
        .field("muted", s.muted)
        .field("firstChannel", s.audioPair + 1)
        .field("device", s.haveAudioDevice)
        .field("bufferMs", s.bufferMs)
        .field("system", s.systemVolume)
        .field("output", s.output);
    j.key("outputs").beginArray();
    for (const auto& o : s.outputs)
        j.beginObject().field("id", o.id).field("name", o.name).field("default", o.isDefault).endObject();
    j.endArray();
    j.endObject();

    j.key("displays").beginArray();
    for (size_t i = 0; i < s.displays.size(); ++i) {
        const auto& d = s.displays[i];
        j.beginObject()
            .field("number", int(i) + 1)
            .field("name", d.name)
            .field("x", d.x).field("y", d.y).field("w", d.w).field("h", d.h)
            .field("refresh", d.refreshHz)
            .field("scale", d.scale)
            .field("primary", d.primary)
            .endObject();
    }
    j.endArray();

    j.key("output").beginObject()
        .field("fullscreen", s.fullscreen)
        .field("wantFullscreen", s.wantFullscreen)
        .field("waiting", s.waiting)
        .field("identify", s.identify)
        .key("target").beginObject()
        .field("number", s.targetIndex >= 0 ? s.targetIndex + 1 : 0)
        .field("name", s.targetName)
        .field("label", s.targetLabel)
        .field("connected", s.targetIndex >= 0)
        .endObject()
        .key("window").beginObject()
        .field("x", s.windowX).field("y", s.windowY).field("w", s.windowW).field("h", s.windowH)
        .field("display", s.windowDisplay >= 0 ? s.windowDisplay + 1 : 0)
        .endObject()
        .field("onTop", s.onTop)
        .field("picture", s.outputPicture)
        .key("controls").beginObject()
        .field("visible", s.controlsVisible)
        .field("panel", s.panel)
        .field("cursor", s.cursorVisible)
        .endObject()
        .key("fade").beginObject()
        .field("active", s.fading)
        .field("waiting", s.fadeWaiting)
        .field("from", s.fadeFrom)
        .endObject();
    if (!s.uiRects.empty()) {
        j.key("ui").beginObject();
        for (const auto& r : s.uiRects) {
            j.key(r.name).beginArray();
            j.value(r.x).value(r.y).value(r.w).value(r.h);
            j.endArray();
        }
        j.endObject();
    }
    j.endObject();

    j.key("settings").beginObject()
        .field("startFullscreen", s.startFullscreen)
        .field("showInfo", s.showInfo)
        .field("cleanOutput", s.cleanOutput)
        .field("alwaysOnTop", s.alwaysOnTop)
        .field("silenceNotifications", s.silenceNotifications)
        .field("fadeMs", s.fadeMs)
        .field("extraIps", s.extraIps)
        .endObject();

    j.key("notifications").beginObject()
        .field("supported", s.notificationsSupported)
        .field("permitted", s.notificationsPermitted)
        .field("silenced", s.notificationsSilenced)
        .field("permissionPending", s.notificationsPermissionPending)
        .field("message", s.notificationsMessage)
        .endObject();

    j.key("remote").beginObject().field("pinRequired", s.pinRequired);
    j.key("urls").beginArray();
    for (const auto& u : s.urls) j.value(u);
    j.endArray();
    j.key("clients").beginArray();
    for (const auto& c : s.clients) j.value(c);
    j.endArray();
    j.endObject();

    j.key("notices").beginArray();
    for (const auto& n : s.notices)
        j.beginObject().field("id", n.id).field("time", n.unixMs).field("text", n.text).endObject();
    j.endArray();

    j.endObject();
    return j.str();
}

std::string remoteLink(const std::string& baseUrl, bool pinRequired, const std::string& pin) {
    std::string url = baseUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url + (pinRequired && !pin.empty() ? "/#pin=" + pin : "/");
}

std::vector<uint8_t> makePreview(const VideoFrame& f, int maxWidth, int& outW, int& outH) {
    outW = outH = 0;
    if (f.width <= 0 || f.height <= 0 || f.pixels.size() < size_t(f.width) * f.height * 4) return {};
    const float aspect = f.aspect > 0 ? f.aspect : float(f.width) / float(f.height);
    int w = std::min(maxWidth, f.width);
    int h = std::max(1, int(std::lround(w / aspect)));
    if (h > f.height) {
        h = f.height;
        w = std::max(1, int(std::lround(h * aspect)));
    }
    std::vector<uint8_t> out(size_t(w) * h * 3);
    // Box filter: average the source pixels each output pixel covers.
    for (int y = 0; y < h; ++y) {
        const int y0 = y * f.height / h, y1 = std::max(y0 + 1, (y + 1) * f.height / h);
        for (int x = 0; x < w; ++x) {
            const int x0 = x * f.width / w, x1 = std::max(x0 + 1, (x + 1) * f.width / w);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const uint8_t* p = f.pixels.data() + (size_t(sy) * f.width + x0) * 4;
                for (int sx = x0; sx < x1; ++sx, p += 4) {
                    const uint32_t a = f.hasAlpha ? p[3] : 255;  // BGRA, over black
                    b += p[0] * a / 255;
                    g += p[1] * a / 255;
                    r += p[2] * a / 255;
                    ++n;
                }
            }
            uint8_t* o = out.data() + (size_t(y) * w + x) * 3;
            o[0] = uint8_t(r / n);
            o[1] = uint8_t(g / n);
            o[2] = uint8_t(b / n);
        }
    }
    outW = w;
    outH = h;
    return out;
}
