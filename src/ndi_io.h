// NDI source discovery and receiving. Both run on their own threads; the UI thread
// only reads snapshots, so a slow network never stalls rendering.
#pragma once

#include "ndi_runtime.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct NdiSource {
    std::string name;  // "MACHINE (Source)"
    std::string url;   // "ip:port" as discovered; may be empty
};

class SourceFinder {
public:
    explicit SourceFinder(const NdiApi& api) : api_(api) {}
    ~SourceFinder() { stop(); }

    // Starts (or restarts) discovery. `extraIps` is a comma separated list of hosts to
    // query directly, for networks where mDNS does not cross subnets/VLANs.
    void start(const std::string& extraIps);
    void stop();

    std::vector<NdiSource> sources() const;  // sorted by name

private:
    void run(std::string extraIps);

    const NdiApi& api_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    std::vector<NdiSource> sources_;
};

// One decoded video frame, always 8-bit BGRA/BGRX, rows tightly packed (stride == width*4).
struct VideoFrame {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
    float aspect = 0.0f;  // display aspect ratio (width / height)
    int frameRateN = 0;
    int frameRateD = 0;
    uint64_t serial = 0;  // increases with every new frame
};

struct ReceiverStatus {
    std::string source;
    bool receiverCreated = false;
    bool connected = false;           // the sender is reachable
    double secondsSinceVideo = -1.0;  // -1 = no frame received yet
    int width = 0, height = 0, frameRateN = 0, frameRateD = 0;
    float measuredFps = 0.0f;
    int audioSampleRate = 0;
    int audioChannels = 0;  // channels the source sends
    int64_t videoFrames = 0;
    int64_t droppedVideoFrames = 0;
};

class Receiver {
public:
    // Called on the capture thread with interleaved stereo float samples.
    using AudioSink = std::function<void(const float* stereo, int frames, int sampleRate)>;

    Receiver(const NdiApi& api, AudioSink sink) : api_(api), sink_(std::move(sink)) {}
    ~Receiver() { connect(std::string()); }

    // Connects to the named source; an empty name disconnects. Passing the URL found by
    // SourceFinder lets the receiver reach sources that were found through extra IPs.
    void connect(const std::string& sourceName, const std::string& url = std::string());
    const std::string& source() const { return source_; }
    const std::string& url() const { return url_; }

    // If a frame newer than `frame.serial` is available, swaps it into `frame` and returns true.
    bool fetchVideo(VideoFrame& frame);

    ReceiverStatus status() const;

    // Which pair of source channels to play: 0 = channels 1-2, 2 = channels 3-4, ...
    void setAudioPair(int firstChannel) { audioPair_ = firstChannel < 0 ? 0 : firstChannel; }
    int audioPair() const { return audioPair_; }

private:
    void run(std::string name, std::string url);
    void handleVideo(const NDIlib_video_frame_v2_t& v);
    void handleAudio(const NDIlib_audio_frame_v3_t& a);

    using Clock = std::chrono::steady_clock;

    const NdiApi& api_;
    AudioSink sink_;
    std::string source_, url_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<int> audioPair_{0};

    mutable std::mutex videoMutex_;
    VideoFrame pending_;
    uint64_t serial_ = 0;

    mutable std::mutex statusMutex_;
    ReceiverStatus status_;
    Clock::time_point lastVideo_{};
    bool haveVideo_ = false;

    std::vector<float> stereo_;  // capture-thread scratch buffer
    int fpsCount_ = 0;
    Clock::time_point fpsWindow_{};
};
