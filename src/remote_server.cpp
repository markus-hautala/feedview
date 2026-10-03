#include "remote_server.h"

#include "jpeg_encode.h"
#include "json_writer.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#include "httplib.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <deque>
#include <future>
#include <mutex>
#include <random>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isIPv4(const std::string& s) {
    int parts = 0, digits = 0;
    for (char c : s) {
        if (c == '.') {
            if (digits == 0) return false;
            ++parts;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 3) return false;
        } else {
            return false;
        }
    }
    return parts == 3 && digits > 0;
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        unsigned char x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= x ^ y;
    }
    return diff == 0;
}

std::string errorJson(const std::string& error, const std::string& message) {
    JsonWriter j;
    j.beginObject().field("ok", false).field("error", error).field("message", message).endObject();
    return j.str();
}

}  // namespace

// ---------------------------------------------------------------------------------------

struct RemoteServer::Impl {
    struct Pending {
        RemoteCommand cmd;
        std::promise<std::pair<RemoteResult, std::string>> done;
    };
    struct Attempts {
        int failures = 0;
        Clock::time_point windowStart{};
        Clock::time_point lockedUntil{};
    };

    // Configuration
    std::string page, hostName, version;
    const unsigned char* font = nullptr;
    size_t fontSize = 0;
    std::vector<std::string> actions;

    // Server
    std::unique_ptr<httplib::Server> svr;
    std::thread thread;
    std::atomic<int> port{0};

    // Auth
    mutable std::mutex authMutex;
    std::string pin;
    bool pinRequired = true;
    std::map<std::string, Attempts> attempts;
    std::map<std::string, Clock::time_point> seen;

    // State / commands
    std::mutex stateMutex;
    std::string state = "{}";
    std::mutex queueMutex;
    std::deque<std::shared_ptr<Pending>> queue;
    std::atomic<bool> accepting{false};

    // Preview
    std::mutex previewMutex;
    std::vector<uint8_t> rgb;
    int pw = 0, ph = 0;
    uint64_t serial = 0, jpegSerial = 0;
    std::vector<uint8_t> jpeg;
    std::atomic<int64_t> lastPreviewRequestMs{-1000000};

    static int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    }

    void markSeen(const std::string& ip) {
        std::lock_guard<std::mutex> lock(authMutex);
        seen[ip] = Clock::now();
    }

    // Returns true if the request may proceed; otherwise fills `res`.
    bool authorize(const httplib::Request& req, httplib::Response& res, bool mutating, bool allowQueryPin) {
        const std::string ip = req.remote_addr;
        std::string expected;
        bool required;
        {
            std::lock_guard<std::mutex> lock(authMutex);
            expected = pin;
            required = pinRequired;
            auto& a = attempts[ip];
            if (Clock::now() < a.lockedUntil) {
                res.status = 429;
                res.set_content(errorJson("locked", "Too many wrong PINs. Try again in a minute."), "application/json");
                return false;
            }
        }
        if (!required) {
            if (!RemoteServer::hostAllowed(req.get_header_value("Host"), hostName)) {
                res.status = 403;
                res.set_content(errorJson("host", "Open the remote with this computer's IP address or name."),
                                "application/json");
                return false;
            }
            if (mutating && !req.has_header("X-FeedView-Pin")) {
                res.status = 403;
                res.set_content(errorJson("header", "Commands need the X-FeedView-Pin header (any value)."),
                                "application/json");
                return false;
            }
            markSeen(ip);
            return true;
        }
        std::string given = req.get_header_value("X-FeedView-Pin");
        if (given.empty() && allowQueryPin) given = req.get_param_value("pin");
        if (!expected.empty() && constantTimeEquals(given, expected)) {
            std::lock_guard<std::mutex> lock(authMutex);
            attempts.erase(ip);
            seen[ip] = Clock::now();
            return true;
        }
        if (!given.empty()) {
            std::lock_guard<std::mutex> lock(authMutex);
            auto& a = attempts[ip];
            const auto now = Clock::now();
            if (now - a.windowStart > std::chrono::seconds(kLockoutSeconds)) {
                a.windowStart = now;
                a.failures = 0;
            }
            if (++a.failures >= kMaxFailures) {
                a.lockedUntil = now + std::chrono::seconds(kLockoutSeconds);
                a.failures = 0;
            }
        }
        res.status = 401;
        res.set_content(errorJson("pin", given.empty() ? "PIN required" : "Wrong PIN"), "application/json");
        return false;
    }

    void fulfilPendingWithError(const std::string& message) {
        std::deque<std::shared_ptr<Pending>> q;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            q.swap(queue);
        }
        for (auto& p : q) p->done.set_value({RemoteResult::fail(message, 503), std::string()});
    }

    void routes(httplib::Server& s) {
        s.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_header("Referrer-Policy", "no-referrer");
            res.set_header("X-Frame-Options", "DENY");
            if (req.path.rfind("/api/", 0) == 0) res.set_header("Cache-Control", "no-store");
        });

        auto servePage = [this](const httplib::Request&, httplib::Response& res) {
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Content-Security-Policy",
                           "default-src 'self'; img-src 'self' blob: data:; style-src 'self' 'unsafe-inline'; "
                           "script-src 'self' 'unsafe-inline'; font-src 'self'; connect-src 'self'; "
                           "frame-ancestors 'none'");
            res.set_content(page, "text/html; charset=utf-8");
        };
        s.Get("/", servePage);
        s.Get("/index.html", servePage);
        s.Get("/font.ttf", [this](const httplib::Request&, httplib::Response& res) {
            if (!font) {
                res.status = 404;
                return;
            }
            res.set_header("Cache-Control", "max-age=86400");
            res.set_content(reinterpret_cast<const char*>(font), fontSize, "font/ttf");
        });

        s.Get("/api/ping", [this](const httplib::Request&, httplib::Response& res) {
            bool required;
            {
                std::lock_guard<std::mutex> lock(authMutex);
                required = pinRequired;
            }
            JsonWriter j;
            j.beginObject()
                .field("app", "FeedView")
                .field("version", version)
                .field("host", hostName)
                .field("pinRequired", required)
                .endObject();
            res.set_content(j.str(), "application/json");
        });

        s.Get("/api/state", [this](const httplib::Request& req, httplib::Response& res) {
            if (!authorize(req, res, false, false)) return;
            std::lock_guard<std::mutex> lock(stateMutex);
            res.set_content(state, "application/json");
        });

        s.Get("/api/preview.jpg", [this](const httplib::Request& req, httplib::Response& res) {
            if (!authorize(req, res, false, true)) return;
            lastPreviewRequestMs = nowMs();
            std::vector<uint8_t> out;
            {
                std::lock_guard<std::mutex> lock(previewMutex);
                if (rgb.empty()) {
                    res.status = 204;
                    return;
                }
                if (jpegSerial != serial || jpeg.empty()) {
                    jpeg = encodeJpeg(rgb.data(), pw, ph, 78);
                    jpegSerial = serial;
                }
                out = jpeg;
            }
            if (out.empty()) {
                res.status = 204;
                return;
            }
            res.set_content(reinterpret_cast<const char*>(out.data()), out.size(), "image/jpeg");
        });

        s.Post(R"(/api/([a-z][a-z0-9\-]*))", [this](const httplib::Request& req, httplib::Response& res) {
            if (!authorize(req, res, true, false)) return;
            const std::string action = req.matches[1];
            if (std::find(actions.begin(), actions.end(), action) == actions.end()) {
                res.status = 404;
                res.set_content(errorJson("action", "Unknown command: " + action), "application/json");
                return;
            }
            auto p = std::make_shared<Pending>();
            p->cmd.action = action;
            for (const auto& kv : req.params) p->cmd.params[kv.first] = kv.second;
            p->cmd.client = req.remote_addr;
            auto fut = p->done.get_future();
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                if (!accepting) {
                    res.status = 503;
                    res.set_content(errorJson("stopping", "FeedView is closing"), "application/json");
                    return;
                }
                queue.push_back(p);
            }
            if (fut.wait_for(std::chrono::milliseconds(kCommandTimeoutMs)) != std::future_status::ready) {
                res.status = 503;
                res.set_content(errorJson("busy", "FeedView did not answer in time"), "application/json");
                return;
            }
            auto [result, stateJson] = fut.get();
            JsonWriter j;
            j.beginObject().field("ok", result.ok).field("message", result.message);
            if (!stateJson.empty()) j.key("state").raw(stateJson);
            j.endObject();
            res.status = result.ok ? 200 : result.status;
            res.set_content(j.str(), "application/json");
        });
    }
};

// ---------------------------------------------------------------------------------------

RemoteServer::RemoteServer() : d_(std::make_unique<Impl>()) {}
RemoteServer::~RemoteServer() { stop(); }

void RemoteServer::setPage(std::string html) { d_->page = std::move(html); }
void RemoteServer::setFont(const unsigned char* data, size_t size) {
    d_->font = data;
    d_->fontSize = size;
}
void RemoteServer::setIdentity(std::string hostName, std::string appVersion) {
    d_->hostName = std::move(hostName);
    d_->version = std::move(appVersion);
}
void RemoteServer::setActions(std::vector<std::string> actions) { d_->actions = std::move(actions); }

int RemoteServer::start(int port, const std::string& bindAddress) {
    stop();
    const int first = port, last = port == 0 ? 0 : port + 9;
    for (int p = first; p <= last; ++p) {
        auto s = std::make_unique<httplib::Server>();
        s->set_payload_max_length(64 * 1024);
        s->set_read_timeout(5, 0);
        s->set_write_timeout(5, 0);
        s->set_keep_alive_timeout(5);
        // Exclusive binding: a second FeedView must get its own port, not share this one.
        s->set_socket_options([](socket_t sock) {  // socket_t: httplib, global namespace
#if defined(_WIN32)
            httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
            httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
        });
        d_->routes(*s);
        int bound = 0;
        if (p == 0) {
            bound = s->bind_to_any_port(bindAddress);
            if (bound <= 0) bound = 0;
        } else if (s->bind_to_port(bindAddress, p)) {
            bound = p;
        }
        if (!bound) continue;
        d_->svr = std::move(s);
        d_->port = bound;
        d_->accepting = true;
        httplib::Server* raw = d_->svr.get();
        d_->thread = std::thread([raw] { raw->listen_after_bind(); });
        d_->svr->wait_until_ready();
        return bound;
    }
    return 0;
}

void RemoteServer::stop() {
    if (!d_->svr) return;
    {
        std::lock_guard<std::mutex> lock(d_->queueMutex);
        d_->accepting = false;
    }
    d_->fulfilPendingWithError("FeedView is closing");
    d_->svr->stop();
    if (d_->thread.joinable()) d_->thread.join();
    d_->svr.reset();
    d_->port = 0;
}

bool RemoteServer::running() const { return d_->svr != nullptr; }
int RemoteServer::port() const { return d_->port; }

void RemoteServer::setAuth(const std::string& pin, bool pinRequired) {
    std::lock_guard<std::mutex> lock(d_->authMutex);
    if (pin != d_->pin || pinRequired != d_->pinRequired) d_->attempts.clear();
    d_->pin = pin;
    d_->pinRequired = pinRequired;
}

int RemoteServer::processCommands(const CommandHandler& handler, const std::function<std::string()>& stateJson) {
    std::deque<std::shared_ptr<Impl::Pending>> q;
    {
        std::lock_guard<std::mutex> lock(d_->queueMutex);
        q.swap(d_->queue);
    }
    if (q.empty()) return 0;
    std::vector<RemoteResult> results;
    results.reserve(q.size());
    for (auto& p : q) results.push_back(handler(p->cmd));
    std::string s = stateJson();
    publishState(s);
    for (size_t i = 0; i < q.size(); ++i) q[i]->done.set_value({results[i], s});
    return int(q.size());
}

void RemoteServer::publishState(std::string json) {
    std::lock_guard<std::mutex> lock(d_->stateMutex);
    d_->state = std::move(json);
}

bool RemoteServer::previewWanted() const { return Impl::nowMs() - d_->lastPreviewRequestMs < 3000; }

void RemoteServer::publishPreview(std::vector<uint8_t> rgb, int width, int height, uint64_t serial) {
    std::lock_guard<std::mutex> lock(d_->previewMutex);
    d_->rgb = std::move(rgb);
    d_->pw = width;
    d_->ph = height;
    d_->serial = serial;
}

void RemoteServer::clearPreview() {
    std::lock_guard<std::mutex> lock(d_->previewMutex);
    d_->rgb.clear();
    d_->jpeg.clear();
}

std::vector<std::string> RemoteServer::recentClients(int seconds) const {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lock(d_->authMutex);
    const auto cutoff = Clock::now() - std::chrono::seconds(seconds);
    for (const auto& [ip, t] : d_->seen)
        if (t >= cutoff) out.push_back(ip);
    return out;
}

std::string RemoteServer::generatePin() {
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 9999);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%04d", dist(rd));
    return buf;
}

bool RemoteServer::hostAllowed(const std::string& hostHeader, const std::string& hostName) {
    std::string h = lower(hostHeader);
    if (h.empty()) return true;  // HTTP/1.0 clients
    if (h[0] == '[') return true;  // IPv6 literal
    const size_t colon = h.rfind(':');
    if (colon != std::string::npos) {
        const std::string portPart = h.substr(colon + 1);
        if (!portPart.empty() && std::all_of(portPart.begin(), portPart.end(), ::isdigit)) h.resize(colon);
    }
    if (h.empty()) return false;
    if (isIPv4(h) || h == "localhost") return true;
    const std::string me = lower(hostName);
    if (!me.empty() && (h == me || h == me + ".local")) return true;
    for (const char* suffix : {".local", ".lan", ".home.arpa", ".internal", ".localdomain"})
        if (endsWith(h, suffix)) return true;
    return false;
}
