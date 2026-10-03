#include "os_control.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <dwmapi.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <sddl.h>
#include <shellapi.h>
#include <userenv.h>
#endif

namespace os {

#if defined(_WIN32)

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string utf8(const wchar_t* w) {
    if (!w || !*w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

template <typename T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Undocumented but stable since Windows 7: the interface the Sound control panel uses to
// change the default device (also used by EarTrumpet, SoundSwitch and others).
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
const CLSID kPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
// PKEY_Device_FriendlyName (defined here so no extra GUID library is needed)
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

}  // namespace

// ---------------------------------------------------------------------------------------

// Windows tells us when outputs or the volume change (on its own threads), so the UI
// thread only re-reads them then: enumerating devices takes ~10 ms, a dropped frame.
class AudioNotifier : public IMMNotificationClient, public IAudioEndpointVolumeCallback {
public:
    std::atomic<bool> devicesChanged{true}, volumeChanged{true};

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient))
            *out = static_cast<IMMNotificationClient*>(this);
        else if (iid == __uuidof(IAudioEndpointVolumeCallback))
            *out = static_cast<IAudioEndpointVolumeCallback*>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return changed(); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return changed(); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return changed(); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole, LPCWSTR) override {
        return flow == eRender ? changed() : S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY key) override {
        // Sent often for other properties; only a renamed device matters here.
        return key.fmtid == kFriendlyName.fmtid && key.pid == kFriendlyName.pid ? changed() : S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
        volumeChanged = true;
        return S_OK;
    }

private:
    HRESULT changed() {
        devicesChanged = true;
        return S_OK;
    }
    std::atomic<ULONG> refs_{1};
};

struct SystemAudio::Impl {
    bool comInit = false;
    IMMDeviceEnumerator* enumerator = nullptr;
    IAudioEndpointVolume* endpoint = nullptr;  // of the current default output
    AudioNotifier* notifier = nullptr;
    bool notified = false;  // Windows sends change notifications; otherwise poll
    std::string defaultId, defaultName, error;
    std::vector<AudioDevice> outputs;
    int volume = -1;
    bool muted = false;
    uint64_t lastVolumeRead = 0, lastDeviceRead = 0;

    void setEndpoint(IAudioEndpointVolume* e) {
        if (endpoint && notifier) endpoint->UnregisterControlChangeNotify(notifier);
        release(endpoint);
        endpoint = e;
        if (endpoint && notifier) endpoint->RegisterControlChangeNotify(notifier);
    }

    std::string deviceId(IMMDevice* dev) {
        LPWSTR id = nullptr;
        std::string out;
        if (SUCCEEDED(dev->GetId(&id))) out = utf8(id);
        CoTaskMemFree(id);
        return out;
    }
    std::string deviceName(IMMDevice* dev) {
        std::string out;
        IPropertyStore* props = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR) out = utf8(v.pwszVal);
            PropVariantClear(&v);
            props->Release();
        }
        return out.empty() ? std::string("Audio output") : out;
    }

    void readDevices() {
        if (!enumerator) return;
        // Default output; re-attach the volume control when it changed.
        IMMDevice* def = nullptr;
        std::string id;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &def))) {
            id = deviceId(def);
            if (id != defaultId || !endpoint) {
                void* p = nullptr;
                setEndpoint(SUCCEEDED(def->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &p))
                                ? static_cast<IAudioEndpointVolume*>(p)
                                : nullptr);
            }
            defaultName = deviceName(def);
            def->Release();
        } else {
            setEndpoint(nullptr);
            defaultName.clear();
        }
        defaultId = id;

        outputs.clear();
        IMMDeviceCollection* all = nullptr;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &all))) {
            UINT n = 0;
            all->GetCount(&n);
            for (UINT i = 0; i < n; ++i) {
                IMMDevice* dev = nullptr;
                if (FAILED(all->Item(i, &dev))) continue;
                AudioDevice d;
                d.id = deviceId(dev);
                d.name = deviceName(dev);
                d.isDefault = d.id == defaultId;
                outputs.push_back(std::move(d));
                dev->Release();
            }
            all->Release();
        }
        std::sort(outputs.begin(), outputs.end(), [](const AudioDevice& a, const AudioDevice& b) { return a.name < b.name; });
    }

    void readVolume() {
        float level = 0;
        BOOL m = FALSE;
        if (endpoint && SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&level)) && SUCCEEDED(endpoint->GetMute(&m))) {
            volume = int(std::lround(level * 100.0f));
            muted = m != FALSE;
        } else {
            volume = -1;
            muted = false;
        }
    }
};

SystemAudio::SystemAudio() : d_(std::make_unique<Impl>()) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    d_->comInit = SUCCEEDED(hr);  // RPC_E_CHANGED_MODE: COM is ready, just not ours to close
    void* p = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), &p))) {
        d_->enumerator = static_cast<IMMDeviceEnumerator*>(p);
        d_->notifier = new AudioNotifier;
        d_->notified = SUCCEEDED(d_->enumerator->RegisterEndpointNotificationCallback(d_->notifier));
    } else {
        d_->error = "Windows audio is not available";
    }
    update(0, true);
}

SystemAudio::~SystemAudio() {
    d_->setEndpoint(nullptr);
    if (d_->notified) d_->enumerator->UnregisterEndpointNotificationCallback(d_->notifier);
    release(d_->notifier);
    release(d_->enumerator);
    if (d_->comInit) CoUninitialize();
}

bool SystemAudio::available() const { return d_->enumerator != nullptr && d_->endpoint != nullptr; }

void SystemAudio::update(uint64_t now, bool force) {
    if (!d_->enumerator) return;
    // With notifications: only when Windows said something changed. Without: poll.
    const bool devices = d_->notified ? d_->notifier->devicesChanged.exchange(false) : now - d_->lastDeviceRead >= 1500;
    if (force || devices) {
        d_->readDevices();
        d_->lastDeviceRead = now;
        force = true;
    }
    const bool volume = d_->notified ? d_->notifier->volumeChanged.exchange(false) : now - d_->lastVolumeRead >= 250;
    if (force || volume) {
        d_->readVolume();
        d_->lastVolumeRead = now;
    }
}

int SystemAudio::volume() const { return d_->volume; }
bool SystemAudio::muted() const { return d_->muted; }
const std::vector<AudioDevice>& SystemAudio::outputs() const { return d_->outputs; }
std::string SystemAudio::defaultOutput() const { return d_->defaultId; }
std::string SystemAudio::defaultOutputName() const { return d_->defaultName; }
std::string SystemAudio::error() const { return d_->error; }

bool SystemAudio::setVolume(int v) {
    if (!d_->endpoint) return false;
    v = std::clamp(v, 0, 100);
    if (FAILED(d_->endpoint->SetMasterVolumeLevelScalar(v / 100.0f, nullptr))) {
        d_->error = "Couldn't change the system volume";
        return false;
    }
    d_->readVolume();
    return true;
}

bool SystemAudio::setMuted(bool m) {
    if (!d_->endpoint) return false;
    if (FAILED(d_->endpoint->SetMute(m ? TRUE : FALSE, nullptr))) {
        d_->error = "Couldn't change the system mute";
        return false;
    }
    d_->readVolume();
    return true;
}

bool SystemAudio::setDefaultOutput(const std::string& id) {
    if (!d_->enumerator) return false;
    d_->readDevices();
    if (std::none_of(d_->outputs.begin(), d_->outputs.end(), [&](const AudioDevice& o) { return o.id == id; })) {
        d_->error = "No such audio output";
        return false;
    }
    IPolicyConfig* policy = nullptr;
    void* p = nullptr;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), &p))) {
        d_->error = "Windows doesn't allow changing the audio output";
        return false;
    }
    policy = static_cast<IPolicyConfig*>(p);
    const std::wstring wid = wide(id);
    const bool ok = SUCCEEDED(policy->SetDefaultEndpoint(wid.c_str(), eConsole)) &&
                    SUCCEEDED(policy->SetDefaultEndpoint(wid.c_str(), eMultimedia));
    policy->Release();
    if (!ok) {
        d_->error = "Couldn't change the audio output";
        return false;
    }
    d_->readDevices();
    d_->readVolume();
    return d_->defaultId == id;
}

// ---------------------------------------------------------------------------------------
// Notifications: the "Turn off toast notifications" user policy. A policy refresh makes
// the notification service apply it right away (verified on Windows 11 25H2).

namespace {

const wchar_t* kPolicyKey = L"Software\\Policies\\Microsoft\\Windows\\CurrentVersion\\PushNotifications";
const wchar_t* kPolicyValue = L"NoToastApplicationNotification";

bool readPolicy(DWORD& value) {
    DWORD size = sizeof value, type = 0;
    return RegGetValueW(HKEY_CURRENT_USER, kPolicyKey, kPolicyValue, RRF_RT_REG_DWORD, &type, &value, &size) ==
           ERROR_SUCCESS;
}

void signalPolicyChange() {
    RefreshPolicy(FALSE);  // user policy: the notification service reloads it
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Policy"), SMTO_ABORTIFHUNG,
                        1000, &result);
}

std::string currentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return std::string();
    std::string out;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buf(size);
    if (size && GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
        LPSTR text = nullptr;
        if (ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &text)) {
            out = text;
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return out;
}

}  // namespace

struct NotificationSilencer::Shared {
    std::atomic<bool> pending{false};
    std::mutex m;
    std::string error;
    void setError(std::string e) {
        std::lock_guard<std::mutex> lock(m);
        error = std::move(e);
    }
};

NotificationSilencer::NotificationSilencer() : s_(std::make_shared<Shared>()) {}
NotificationSilencer::~NotificationSilencer() = default;

bool NotificationSilencer::supported() const { return true; }

bool NotificationSilencer::permitted() const {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPolicyKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

bool NotificationSilencer::isSilenced() const {
    DWORD v = 0;
    return readPolicy(v) && v != 0;
}

bool NotificationSilencer::silence(bool on, bool wait) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPolicyKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        s_->setError("FeedView needs a one-time permission to switch notifications off");
        return false;
    }
    LONG r;
    if (on) {
        const DWORD one = 1;
        r = RegSetValueExW(key, kPolicyValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof one);
    } else {
        r = RegDeleteValueW(key, kPolicyValue);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (r != ERROR_SUCCESS) {
        s_->setError("Couldn't change the notification setting");
        return false;
    }
    if (wait)
        signalPolicyChange();
    else
        std::thread(signalPolicyChange).detach();
    return true;
}

void NotificationSilencer::requestPermission() {
    if (s_->pending.exchange(true)) return;
    std::thread([s = s_] {
        const std::string sid = currentUserSid();
        wchar_t exe[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
        const std::wstring args = L"--grant-notification-control " + wide(sid);
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof sei;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb = L"runas";
        sei.lpFile = exe;
        sei.lpParameters = args.c_str();
        sei.nShow = SW_HIDE;
        if (sid.empty()) {
            s->setError("Couldn't identify the Windows user");
        } else if (!ShellExecuteExW(&sei)) {
            s->setError(GetLastError() == ERROR_CANCELLED ? "The permission was not given (the Windows prompt was declined)"
                                                          : "Couldn't ask Windows for the permission");
        } else {
            DWORD code = 1;
            if (sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 60000);
                GetExitCodeProcess(sei.hProcess, &code);
                CloseHandle(sei.hProcess);
            }
            s->setError(code == 0 ? std::string() : "Windows didn't accept the permission change");
        }
        s->pending = false;
    }).detach();
}

bool NotificationSilencer::permissionPending() const { return s_->pending; }

std::string NotificationSilencer::lastError() const {
    std::lock_guard<std::mutex> lock(s_->m);
    return s_->error;
}

bool grantNotificationControl(const std::string& sidText) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidA(sidText.c_str(), &sid)) return false;
    // HKEY_USERS\<sid> is that user's HKEY_CURRENT_USER, also when another admin account
    // approved the prompt.
    const std::wstring path = wide(sidText) + L"\\" + kPolicyKey;
    HKEY key = nullptr;
    bool ok = RegCreateKeyExW(HKEY_USERS, path.c_str(), 0, nullptr, 0, READ_CONTROL | WRITE_DAC, nullptr, &key,
                              nullptr) == ERROR_SUCCESS;
    if (ok) {
        PACL oldDacl = nullptr, newDacl = nullptr;
        PSECURITY_DESCRIPTOR sd = nullptr;
        ok = GetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldDacl, nullptr,
                             &sd) == ERROR_SUCCESS;
        if (ok) {
            EXPLICIT_ACCESSW ea{};
            ea.grfAccessPermissions = KEY_READ | KEY_SET_VALUE;  // only this key, no subkeys
            ea.grfAccessMode = GRANT_ACCESS;
            ea.grfInheritance = NO_INHERITANCE;
            ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
            ea.Trustee.ptstrName = static_cast<LPWSTR>(sid);
            ok = SetEntriesInAclW(1, &ea, oldDacl, &newDacl) == ERROR_SUCCESS &&
                 SetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl,
                                 nullptr) == ERROR_SUCCESS;
        }
        if (newDacl) LocalFree(newDacl);
        if (sd) LocalFree(sd);
        RegCloseKey(key);
    }
    LocalFree(sid);
    return ok;
}

std::string notificationPolicyValueForTests() {
    DWORD v = 0;
    return readPolicy(v) ? std::to_string(v) : std::string();
}

// ---------------------------------------------------------------------------------------
// Window stacking

namespace {

RECT visibleRect(HWND w) {
    RECT r{};
    // Without the invisible resize borders (they overhang onto neighbouring screens).
    if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(w, &r);
    return r;
}

bool drawsNothing(HWND w) {
    const LONG ex = GetWindowLongW(w, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return true;  // click-through overlays
    if (ex & WS_EX_LAYERED) {
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(w, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0) return true;
    }
    BOOL cloaked = FALSE;  // hidden store apps, other virtual desktops
    return SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked;
}

}  // namespace

void raiseWindow(void* nativeWindow, bool topmost) {
    HWND h = static_cast<HWND>(nativeWindow);
    if (!h) return;
    if (IsIconic(h))
        ShowWindow(h, SW_SHOWNOACTIVATE);
    else if (!IsWindowVisible(h))
        ShowWindow(h, SW_SHOWNA);
    // (With SWP_SHOWWINDOW, Windows makes it topmost but doesn't move it above the others.)
    SetWindowPos(h, topmost ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

bool coveredByOtherWindow(void* nativeWindow) {
    HWND h = static_cast<HWND>(nativeWindow);
    if (!h || !IsWindowVisible(h) || IsIconic(h)) return false;
    const DWORD thread = GetWindowThreadProcessId(h, nullptr);
    RECT me{};
    GetWindowRect(h, &me);
    for (HWND w = GetWindow(h, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV)) {
        if (!IsWindowVisible(w) || IsIconic(w) || GetWindowThreadProcessId(w, nullptr) == thread) continue;
        if (drawsNothing(w)) continue;
        const RECT r = visibleRect(w);
        RECT x{};
        if (!IntersectRect(&x, &r, &me) || x.right - x.left < 4 || x.bottom - x.top < 4) continue;
        return true;
    }
    return false;
}

bool keepOnTop(void* nativeWindow) {
    if (!coveredByOtherWindow(nativeWindow)) return false;
    SetWindowPos(static_cast<HWND>(nativeWindow), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    return true;
}

bool isTopmost(void* nativeWindow) {
    HWND h = static_cast<HWND>(nativeWindow);
    return h && (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

#else  // ---------------------------------------------------------------- other systems

struct SystemAudio::Impl {
    std::vector<AudioDevice> none;
};
SystemAudio::SystemAudio() : d_(std::make_unique<Impl>()) {}
SystemAudio::~SystemAudio() = default;
bool SystemAudio::available() const { return false; }
void SystemAudio::update(uint64_t, bool) {}
int SystemAudio::volume() const { return -1; }
bool SystemAudio::muted() const { return false; }
const std::vector<AudioDevice>& SystemAudio::outputs() const { return d_->none; }
std::string SystemAudio::defaultOutput() const { return std::string(); }
std::string SystemAudio::defaultOutputName() const { return std::string(); }
bool SystemAudio::setVolume(int) { return false; }
bool SystemAudio::setMuted(bool) { return false; }
bool SystemAudio::setDefaultOutput(const std::string&) { return false; }
std::string SystemAudio::error() const { return "Not available on this operating system"; }

struct NotificationSilencer::Shared {};
NotificationSilencer::NotificationSilencer() : s_(std::make_shared<Shared>()) {}
NotificationSilencer::~NotificationSilencer() = default;
bool NotificationSilencer::supported() const { return false; }
bool NotificationSilencer::permitted() const { return false; }
bool NotificationSilencer::isSilenced() const { return false; }
bool NotificationSilencer::silence(bool, bool) { return false; }
void NotificationSilencer::requestPermission() {}
bool NotificationSilencer::permissionPending() const { return false; }
std::string NotificationSilencer::lastError() const { return "Not available on this operating system"; }
bool grantNotificationControl(const std::string&) { return false; }
std::string notificationPolicyValueForTests() { return std::string(); }

void raiseWindow(void*, bool) {}
bool coveredByOtherWindow(void*) { return false; }
bool keepOnTop(void*) { return false; }
bool isTopmost(void*) { return false; }

#endif

}  // namespace os
