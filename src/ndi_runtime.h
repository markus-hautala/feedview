// Loads the NDI(R) runtime library at run time and resolves the functions FeedView uses.
//
// Functions are looked up one by one by their exported names instead of through
// NDIlib_vX_load(): older 6.x runtimes (e.g. 6.1) do not export NDIlib_v6_load, while
// every NDI 5 and 6 runtime exports the plain symbol names used here.
#pragma once

// The NDI headers use NULL and fixed-width ints without including their headers.
#include <cstddef>
#include <cstdint>

#include <Processing.NDI.Lib.h>

#include <string>
#include <vector>

struct NdiApi {
    decltype(&::NDIlib_initialize) initialize = nullptr;
    decltype(&::NDIlib_destroy) destroy = nullptr;
    decltype(&::NDIlib_version) version = nullptr;

    decltype(&::NDIlib_find_create_v2) find_create_v2 = nullptr;
    decltype(&::NDIlib_find_destroy) find_destroy = nullptr;
    decltype(&::NDIlib_find_wait_for_sources) find_wait_for_sources = nullptr;
    decltype(&::NDIlib_find_get_current_sources) find_get_current_sources = nullptr;

    decltype(&::NDIlib_recv_create_v3) recv_create_v3 = nullptr;
    decltype(&::NDIlib_recv_destroy) recv_destroy = nullptr;
    decltype(&::NDIlib_recv_capture_v3) recv_capture_v3 = nullptr;
    decltype(&::NDIlib_recv_free_video_v2) recv_free_video_v2 = nullptr;
    decltype(&::NDIlib_recv_free_audio_v3) recv_free_audio_v3 = nullptr;
    decltype(&::NDIlib_recv_free_metadata) recv_free_metadata = nullptr;
    decltype(&::NDIlib_recv_get_no_connections) recv_get_no_connections = nullptr;
    decltype(&::NDIlib_recv_get_performance) recv_get_performance = nullptr;

    // Only used by the test-pattern sender tool.
    decltype(&::NDIlib_send_create) send_create = nullptr;
    decltype(&::NDIlib_send_destroy) send_destroy = nullptr;
    decltype(&::NDIlib_send_send_video_v2) send_send_video_v2 = nullptr;
    decltype(&::NDIlib_send_send_audio_v3) send_send_audio_v3 = nullptr;
};

class NdiRuntime {
public:
    // Tries the bundled copy next to the app first, then an installed NDI Runtime / SDK.
    // `appDir` is the directory of the executable (with or without a trailing separator).
    bool load(const std::string& appDir);
    void unload();
    ~NdiRuntime() { unload(); }

    const NdiApi& api() const { return api_; }
    bool loaded() const { return handle_ != nullptr; }

    const std::string& libraryPath() const { return libraryPath_; }
    const std::string& versionString() const { return version_; }
    const std::string& error() const { return error_; }
    const std::vector<std::string>& triedPaths() const { return tried_; }

    // Where people can download the runtime for this platform.
    static const char* downloadUrl();

private:
    bool tryLoad(const std::string& path);
    bool resolveAll();

    void* handle_ = nullptr;
    NdiApi api_{};
    std::string libraryPath_, version_, error_;
    std::vector<std::string> tried_;
};
