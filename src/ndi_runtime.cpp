#include "ndi_runtime.h"

#include <cstdlib>
#include <type_traits>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

std::string joinPath(std::string dir, const std::string& file) {
    if (dir.empty()) return file;
    char last = dir.back();
    if (last != '/' && last != '\\') dir += '/';
    return dir + file;
}

std::string envDir(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
void* openLib(const std::string& path) {
    // LOAD_WITH_ALTERED_SEARCH_PATH lets the DLL find its own dependencies next to it.
    UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
    HMODULE h = LoadLibraryExW(widen(path).c_str(), nullptr,
                               path.find_first_of("/\\") != std::string::npos ? LOAD_WITH_ALTERED_SEARCH_PATH : 0);
    SetErrorMode(oldMode);
    return (void*)h;
}
void* findSym(void* h, const char* name) { return (void*)GetProcAddress((HMODULE)h, name); }
void closeLib(void* h) { FreeLibrary((HMODULE)h); }
#else
void* openLib(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* findSym(void* h, const char* name) { return dlsym(h, name); }
void closeLib(void* h) { dlclose(h); }
#endif

}  // namespace

const char* NdiRuntime::downloadUrl() {
#if defined(__APPLE__)
    return "https://ndi.video/tools/";
#elif defined(_WIN32)
    return "https://ndi.video/tools/";
#else
    return "https://ndi.video/for-developers/ndi-sdk/";
#endif
}

bool NdiRuntime::load(const std::string& appDir) {
    if (handle_) return true;
    tried_.clear();
    error_.clear();

    std::vector<std::string> candidates;

    // Explicit override, mostly for testing a specific runtime build.
    if (auto p = envDir("FEEDVIEW_NDI_LIBRARY"); !p.empty()) candidates.push_back(p);

#if defined(_WIN32)
    const std::string lib = NDILIB_LIBRARY_NAME;  // Processing.NDI.Lib.x64.dll
    candidates.push_back(joinPath(appDir, lib));
    if (auto d = envDir("NDI_RUNTIME_DIR_V6"); !d.empty()) candidates.push_back(joinPath(d, lib));
    if (auto d = envDir("NDI_RUNTIME_DIR_V5"); !d.empty()) candidates.push_back(joinPath(d, lib));
    candidates.push_back(lib);  // normal DLL search path
#elif defined(__APPLE__)
    const std::string lib = "libndi.dylib";
    // SDL_GetBasePath() points at Contents/Resources inside an .app bundle.
    candidates.push_back(joinPath(appDir, "../Frameworks/" + lib));
    candidates.push_back(joinPath(appDir, lib));
    if (auto d = envDir("NDI_RUNTIME_DIR_V6"); !d.empty()) candidates.push_back(joinPath(d, lib));
    candidates.push_back("/usr/local/lib/" + lib);
    candidates.push_back("/Library/NDI SDK for Apple/lib/macOS/" + lib);
    candidates.push_back("/Library/NDI SDK for Apple/lib/macOS/libndi_advanced.dylib");
    candidates.push_back(lib);
#else
    candidates.push_back(joinPath(appDir, "lib/libndi.so.6"));
    candidates.push_back(joinPath(appDir, "libndi.so.6"));
    if (auto d = envDir("NDI_RUNTIME_DIR_V6"); !d.empty()) candidates.push_back(joinPath(d, "libndi.so.6"));
    candidates.push_back("libndi.so.6");
    candidates.push_back("/usr/local/lib/libndi.so.6");
    candidates.push_back("/usr/lib/libndi.so.6");
    candidates.push_back("libndi.so.5");
#endif

    for (const auto& c : candidates) {
        if (tryLoad(c)) return true;
    }
    if (error_.empty()) error_ = "The NDI runtime library could not be found.";
    return false;
}

bool NdiRuntime::tryLoad(const std::string& path) {
    tried_.push_back(path);
    void* h = openLib(path);
    if (!h) return false;
    handle_ = h;
    if (!resolveAll()) {
        closeLib(h);
        handle_ = nullptr;
        api_ = NdiApi{};
        return false;
    }
    if (!api_.initialize()) {
        error_ = "The NDI runtime at " + path + " refused to start (the CPU may not be supported).";
        closeLib(h);
        handle_ = nullptr;
        api_ = NdiApi{};
        return false;
    }
    libraryPath_ = path;
    const char* v = api_.version ? api_.version() : nullptr;
    version_ = v ? v : "";
    return true;
}

bool NdiRuntime::resolveAll() {
    bool ok = true;
    auto req = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(findSym(handle_, name));
        if (!fn) {
            ok = false;
            error_ = std::string("The NDI runtime is too old: missing ") + name + ".";
        }
    };
    auto opt = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(findSym(handle_, name));
    };

    req(api_.initialize, "NDIlib_initialize");
    req(api_.destroy, "NDIlib_destroy");
    opt(api_.version, "NDIlib_version");

    req(api_.find_create_v2, "NDIlib_find_create_v2");
    req(api_.find_destroy, "NDIlib_find_destroy");
    req(api_.find_wait_for_sources, "NDIlib_find_wait_for_sources");
    req(api_.find_get_current_sources, "NDIlib_find_get_current_sources");

    req(api_.recv_create_v3, "NDIlib_recv_create_v3");
    req(api_.recv_destroy, "NDIlib_recv_destroy");
    req(api_.recv_capture_v3, "NDIlib_recv_capture_v3");
    req(api_.recv_free_video_v2, "NDIlib_recv_free_video_v2");
    req(api_.recv_free_audio_v3, "NDIlib_recv_free_audio_v3");
    req(api_.recv_free_metadata, "NDIlib_recv_free_metadata");
    req(api_.recv_get_no_connections, "NDIlib_recv_get_no_connections");
    opt(api_.recv_get_performance, "NDIlib_recv_get_performance");

    opt(api_.send_create, "NDIlib_send_create");
    opt(api_.send_destroy, "NDIlib_send_destroy");
    opt(api_.send_send_video_v2, "NDIlib_send_send_video_v2");
    opt(api_.send_send_audio_v3, "NDIlib_send_send_audio_v3");
    return ok;
}

void NdiRuntime::unload() {
    if (!handle_) return;
    if (api_.destroy) api_.destroy();
    closeLib(handle_);
    handle_ = nullptr;
    api_ = NdiApi{};
}
