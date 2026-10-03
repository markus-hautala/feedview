// The web remote: a small HTTP server inside FeedView that serves the control page and a
// JSON API on the local network.
//
// Threading: requests are handled on the server's own threads. Anything that changes
// FeedView is queued as a RemoteCommand and executed by the main (UI) thread in
// processCommands(); the request waits for the result and gets the fresh state back.
// The main thread also publishes a state snapshot and preview frames; requests only read
// those, so a slow phone never stalls the picture.
//
// Security model (local network, plain HTTP):
//  * With a PIN (off by default, turned on in FeedView's Remote panel), every API call must
//    carry it (header X-FeedView-Pin). Wrong guesses are rate limited per address.
//  * Commands always need that header, even without a PIN, so a web page opened on some
//    other computer cannot trigger them (custom headers can't be sent cross-site).
//  * Without a PIN, requests must use an IP address or a local host name, which blocks
//    DNS-rebinding tricks from outside web sites.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct RemoteCommand {
    std::string action;  // "fullscreen", "display", "source", ...
    std::map<std::string, std::string> params;
    std::string client;  // remote address

    bool has(const std::string& k) const { return params.count(k) != 0; }
    std::string param(const std::string& k, const std::string& def = std::string()) const {
        auto it = params.find(k);
        return it == params.end() ? def : it->second;
    }
};

struct RemoteResult {
    bool ok = true;
    std::string message;
    int status = 200;  // HTTP status when !ok (400 bad parameter, 409 not possible now...)

    static RemoteResult fail(std::string msg, int status = 400) { return {false, std::move(msg), status}; }
};

class RemoteServer {
public:
    RemoteServer();
    ~RemoteServer();
    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    // Content and identity (set before start()).
    void setPage(std::string html);
    void setFont(const unsigned char* data, size_t size);
    void setIdentity(std::string hostName, std::string appVersion);
    // Commands the API accepts (POST /api/<action>); anything else is a 404.
    void setActions(std::vector<std::string> actions);

    // Starts listening on `port` or, if that is taken, the next free one up to port+9.
    // port 0 picks any free port (tests). Returns the port in use, or 0 on failure.
    int start(int port, const std::string& bindAddress = "0.0.0.0");
    void stop();
    bool running() const;
    int port() const;

    void setAuth(const std::string& pin, bool pinRequired);  // thread safe, any time

    // ---- Main thread
    using CommandHandler = std::function<RemoteResult(const RemoteCommand&)>;
    // Runs queued commands, publishes stateJson() and answers the waiting requests.
    int processCommands(const CommandHandler& handler, const std::function<std::string()>& stateJson);
    void publishState(std::string json);
    // True while a page is showing the live preview (asked within the last few seconds).
    bool previewWanted() const;
    // 8-bit RGB, rows packed. `serial` changes with every new picture.
    void publishPreview(std::vector<uint8_t> rgb, int width, int height, uint64_t serial);
    void clearPreview();
    // Addresses that used the remote within the last `seconds`.
    std::vector<std::string> recentClients(int seconds) const;

    static std::string generatePin();
    static bool hostAllowed(const std::string& hostHeader, const std::string& hostName);

    static constexpr int kMaxFailures = 5;          // wrong PINs per address...
    static constexpr int kLockoutSeconds = 60;      // ...before a pause
    static constexpr int kCommandTimeoutMs = 3000;  // main thread must answer within this

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};
