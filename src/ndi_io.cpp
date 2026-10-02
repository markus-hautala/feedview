#include "ndi_io.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------------------
// SourceFinder

void SourceFinder::start(const std::string& extraIps) {
    stop();
    stop_ = false;
    thread_ = std::thread(&SourceFinder::run, this, extraIps);
}

void SourceFinder::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<NdiSource> SourceFinder::sources() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sources_;
}

void SourceFinder::run(std::string extraIps) {
    NDIlib_find_create_t cfg;
    cfg.show_local_sources = true;
    cfg.p_groups = nullptr;
    cfg.p_extra_ips = extraIps.empty() ? nullptr : extraIps.c_str();

    NDIlib_find_instance_t finder = api_.find_create_v2(&cfg);
    if (!finder) return;

    while (!stop_) {
        // Short timeout so stop() never waits long.
        api_.find_wait_for_sources(finder, 250);
        uint32_t count = 0;
        const NDIlib_source_t* list = api_.find_get_current_sources(finder, &count);
        std::vector<NdiSource> names;
        names.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (list[i].p_ndi_name)
                names.push_back({list[i].p_ndi_name, list[i].p_url_address ? list[i].p_url_address : ""});
        }
        std::sort(names.begin(), names.end(),
                  [](const NdiSource& a, const NdiSource& b) { return a.name < b.name; });
        std::lock_guard<std::mutex> lock(mutex_);
        sources_.swap(names);
    }
    api_.find_destroy(finder);
}

// ---------------------------------------------------------------------------------------
// Receiver

void Receiver::connect(const std::string& sourceName, const std::string& url) {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    stop_ = false;

    source_ = sourceName;
    url_ = sourceName.empty() ? std::string() : url;
    {
        std::lock_guard<std::mutex> lock(videoMutex_);
        pending_ = VideoFrame{};
    }
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        status_ = ReceiverStatus{};
        status_.source = sourceName;
        haveVideo_ = false;
    }
    if (!sourceName.empty()) thread_ = std::thread(&Receiver::run, this, sourceName, url_);
}

bool Receiver::fetchVideo(VideoFrame& frame) {
    std::lock_guard<std::mutex> lock(videoMutex_);
    if (serial_ == 0 || pending_.serial == 0 || pending_.serial <= frame.serial) return false;
    // Swap rather than copy: the capture thread reuses the old buffer's capacity.
    std::swap(frame, pending_);
    return true;
}

ReceiverStatus Receiver::status() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    ReceiverStatus s = status_;
    if (haveVideo_) s.secondsSinceVideo = std::chrono::duration<double>(Clock::now() - lastVideo_).count();
    return s;
}

void Receiver::run(std::string name, std::string url) {
    NDIlib_recv_create_v3_t cfg;
    cfg.source_to_connect_to.p_ndi_name = name.c_str();
    cfg.source_to_connect_to.p_url_address = url.empty() ? nullptr : url.c_str();
    // BGRA output: NDI converts with the right BT.601/709 matrix on this thread and every
    // renderer (Direct3D, Metal, OpenGL) can upload it without further conversion.
    cfg.color_format = NDIlib_recv_color_format_BGRX_BGRA;
    cfg.bandwidth = NDIlib_recv_bandwidth_highest;
    cfg.allow_video_fields = false;  // deliver progressive frames
    cfg.p_ndi_recv_name = "FeedView";

    NDIlib_recv_instance_t recv = api_.recv_create_v3(&cfg);
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        status_.receiverCreated = recv != nullptr;
    }
    if (!recv) return;

    fpsCount_ = 0;
    fpsWindow_ = Clock::now();
    auto lastPoll = Clock::time_point{};

    while (!stop_) {
        NDIlib_video_frame_v2_t video;
        NDIlib_audio_frame_v3_t audio;
        NDIlib_metadata_frame_t meta;
        switch (api_.recv_capture_v3(recv, &video, &audio, &meta, 50)) {
            case NDIlib_frame_type_video:
                handleVideo(video);
                api_.recv_free_video_v2(recv, &video);
                break;
            case NDIlib_frame_type_audio:
                handleAudio(audio);
                api_.recv_free_audio_v3(recv, &audio);
                break;
            case NDIlib_frame_type_metadata:
                api_.recv_free_metadata(recv, &meta);
                break;
            default:
                break;
        }

        auto now = Clock::now();
        if (now - lastPoll >= std::chrono::milliseconds(250)) {
            lastPoll = now;
            bool connected = api_.recv_get_no_connections(recv) > 0;
            NDIlib_recv_performance_t total{}, dropped{};
            if (api_.recv_get_performance) api_.recv_get_performance(recv, &total, &dropped);
            double window = std::chrono::duration<double>(now - fpsWindow_).count();
            std::lock_guard<std::mutex> lock(statusMutex_);
            status_.connected = connected;
            status_.videoFrames = total.video_frames;
            status_.droppedVideoFrames = dropped.video_frames;
            if (window >= 1.0) {
                status_.measuredFps = float(fpsCount_ / window);
                fpsCount_ = 0;
                fpsWindow_ = now;
            }
        }
    }
    api_.recv_destroy(recv);
}

void Receiver::handleVideo(const NDIlib_video_frame_v2_t& v) {
    const bool bgra = v.FourCC == NDIlib_FourCC_video_type_BGRA;
    const bool bgrx = v.FourCC == NDIlib_FourCC_video_type_BGRX;
    if ((!bgra && !bgrx) || !v.p_data || v.xres <= 0 || v.yres <= 0) return;

    const int rowBytes = v.xres * 4;
    const int stride = v.line_stride_in_bytes > 0 ? v.line_stride_in_bytes : rowBytes;
    {
        std::lock_guard<std::mutex> lock(videoMutex_);
        VideoFrame& f = pending_;
        f.pixels.resize(size_t(rowBytes) * size_t(v.yres));
        if (stride == rowBytes) {
            std::memcpy(f.pixels.data(), v.p_data, f.pixels.size());
        } else {
            for (int y = 0; y < v.yres; ++y)
                std::memcpy(f.pixels.data() + size_t(y) * rowBytes, v.p_data + size_t(y) * stride, rowBytes);
        }
        f.width = v.xres;
        f.height = v.yres;
        f.hasAlpha = bgra;
        f.aspect = v.picture_aspect_ratio > 0.0f ? v.picture_aspect_ratio : float(v.xres) / float(v.yres);
        f.frameRateN = v.frame_rate_N;
        f.frameRateD = v.frame_rate_D;
        f.serial = ++serial_;
    }
    ++fpsCount_;
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_.width = v.xres;
    status_.height = v.yres;
    status_.frameRateN = v.frame_rate_N;
    status_.frameRateD = v.frame_rate_D;
    lastVideo_ = Clock::now();
    haveVideo_ = true;
}

void Receiver::handleAudio(const NDIlib_audio_frame_v3_t& a) {
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        status_.audioSampleRate = a.sample_rate;
        status_.audioChannels = a.no_channels;
    }
    // recv_capture_v3 delivers planar 32-bit float (FLTP).
    if (!sink_ || a.FourCC != NDIlib_FourCC_audio_type_FLTP || !a.p_data || a.no_samples <= 0 ||
        a.no_channels <= 0 || a.sample_rate <= 0)
        return;

    const int channels = a.no_channels;
    int left = audioPair_;
    if (left >= channels) left = 0;  // selected pair not present in this source
    const int right = (left + 1 < channels) ? left + 1 : left;  // mono sources play on both sides
    const int stride = a.channel_stride_in_bytes;

    const float* l = reinterpret_cast<const float*>(a.p_data + size_t(left) * stride);
    const float* r = reinterpret_cast<const float*>(a.p_data + size_t(right) * stride);
    stereo_.resize(size_t(a.no_samples) * 2);
    for (int i = 0; i < a.no_samples; ++i) {
        stereo_[2 * i] = l[i];
        stereo_[2 * i + 1] = r[i];
    }
    sink_(stereo_.data(), a.no_samples, a.sample_rate);
}
