// Tests the web remote's HTTP layer: authentication, rate limiting, cross-site and
// DNS-rebinding protection, command round trips, state and preview delivery.

#include "remote_server.h"

#include "httplib.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

static int gFailures = 0, gChecks = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        ++gChecks;                                                         \
        if (!(cond)) {                                                     \
            ++gFailures;                                                   \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)
#define SCENARIO(name) std::printf("- %s\n", name)

static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

int main() {
    RemoteServer srv;
    srv.setPage("<!doctype html><title>FeedView</title>");
    srv.setIdentity("STUDIO-LAPTOP", "1.2.3");
    srv.setActions({"fullscreen", "source", "slow"});
    srv.setAuth("4821", true);
    const int port = srv.start(0, "127.0.0.1");
    CHECK(port > 0);

    // A pretend main loop: executes commands like FeedView's UI thread would.
    std::atomic<bool> run{true};
    std::atomic<int> executed{0};
    std::string fullscreen = "false";
    std::thread mainLoop([&] {
        while (run) {
            srv.processCommands(
                [&](const RemoteCommand& c) -> RemoteResult {
                    ++executed;
                    if (c.action == "fullscreen") {
                        fullscreen = c.param("on") == "1" ? "true" : "false";
                        return {true, "Fullscreen " + std::string(fullscreen == "true" ? "on" : "off")};
                    }
                    if (c.action == "source") {
                        if (c.param("name").empty()) return RemoteResult::fail("Missing name");
                        return {true, "Showing " + c.param("name")};
                    }
                    if (c.action == "slow") std::this_thread::sleep_for(std::chrono::milliseconds(4000));
                    return {true, "ok"};
                },
                [&] { return std::string("{\"fullscreen\":") + fullscreen + "}"; });
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    httplib::Client cli("127.0.0.1", port);
    cli.set_read_timeout(10, 0);
    const httplib::Headers good = {{"X-FeedView-Pin", "4821"}};
    const httplib::Headers bad = {{"X-FeedView-Pin", "0000"}};

    {
        SCENARIO("page, ping and security headers are served without a PIN");
        auto r = cli.Get("/");
        CHECK(r && r->status == 200 && contains(r->body, "FeedView"));
        CHECK(r && r->get_header_value("X-Frame-Options") == "DENY");
        CHECK(r && contains(r->get_header_value("Content-Security-Policy"), "frame-ancestors 'none'"));
        auto p = cli.Get("/api/ping");
        CHECK(p && p->status == 200 && contains(p->body, "\"pinRequired\":true") && contains(p->body, "STUDIO-LAPTOP"));
    }
    {
        SCENARIO("state needs the PIN");
        auto r = cli.Get("/api/state");
        CHECK(r && r->status == 401 && contains(r->body, "PIN required"));
        r = cli.Get("/api/state", bad);
        CHECK(r && r->status == 401 && contains(r->body, "Wrong PIN"));
        r = cli.Get("/api/state", good);
        CHECK(r && r->status == 200 && r->body == "{}");
        CHECK(r && r->get_header_value("Cache-Control") == "no-store");
    }
    {
        SCENARIO("command round trip runs on the main thread and returns the new state");
        auto r = cli.Post("/api/fullscreen", good, "on=1", "application/x-www-form-urlencoded");
        CHECK(r && r->status == 200);
        CHECK(r && contains(r->body, "\"message\":\"Fullscreen on\""));
        CHECK(r && contains(r->body, "\"state\":{\"fullscreen\":true}"));
        r = cli.Get("/api/state", good);
        CHECK(r && r->body == "{\"fullscreen\":true}");
        r = cli.Post("/api/fullscreen?on=0", good, "", "application/x-www-form-urlencoded");  // query params work too
        CHECK(r && contains(r->body, "Fullscreen off"));
    }
    {
        SCENARIO("handler errors come back with their status; unknown commands never reach the main thread");
        auto r = cli.Post("/api/source", good, "", "application/x-www-form-urlencoded");
        CHECK(r && r->status == 400 && contains(r->body, "Missing name"));
        r = cli.Post("/api/source", good, "name=STUDIO%20(Program)", "application/x-www-form-urlencoded");
        CHECK(r && r->status == 200 && contains(r->body, "Showing STUDIO (Program)"));
        const int before = executed;
        r = cli.Post("/api/format-disk", good, "", "application/x-www-form-urlencoded");
        CHECK(r && r->status == 404);
        CHECK(executed == before);
    }
    {
        SCENARIO("a stuck main thread gives a clean 503 instead of hanging the phone");
        auto r = cli.Post("/api/slow", good, "", "application/x-www-form-urlencoded");
        CHECK(r && r->status == 503 && contains(r->body, "did not answer"));
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // let the slow command finish
    }
    {
        SCENARIO("preview: 204 until a picture exists, then a JPEG; PIN accepted as query parameter for <img>");
        CHECK(!srv.previewWanted());
        auto r = cli.Get("/api/preview.jpg", good);
        CHECK(r && r->status == 204);
        CHECK(srv.previewWanted());
        std::vector<uint8_t> rgb(64 * 36 * 3, 0);
        for (size_t i = 0; i < rgb.size(); i += 3) rgb[i] = 200;  // red
        srv.publishPreview(rgb, 64, 36, 1);
        r = cli.Get("/api/preview.jpg?pin=4821");
        CHECK(r && r->status == 200 && r->get_header_value("Content-Type") == "image/jpeg");
        CHECK(r && r->body.size() > 100 && (unsigned char)r->body[0] == 0xFF && (unsigned char)r->body[1] == 0xD8);
        srv.clearPreview();
        r = cli.Get("/api/preview.jpg", good);
        CHECK(r && r->status == 204);
    }
    {
        SCENARIO("wrong PINs are rate limited per address, and a lockout also blocks the right PIN");
        for (int i = 0; i < RemoteServer::kMaxFailures; ++i) cli.Get("/api/state", bad);
        auto r = cli.Get("/api/state", good);
        CHECK(r && r->status == 429);
        srv.setAuth("4821", true);  // unchanged PIN: lockout stays
        r = cli.Get("/api/state", good);
        CHECK(r && r->status == 429);
        srv.setAuth("5555", true);  // operator sets a new PIN: lockouts reset
        r = cli.Get("/api/state", httplib::Headers{{"X-FeedView-Pin", "5555"}});
        CHECK(r && r->status == 200);
    }
    {
        SCENARIO("without a PIN: reading works, commands still need the header (blocks cross-site forms)");
        srv.setAuth("5555", false);
        auto r = cli.Get("/api/state");
        CHECK(r && r->status == 200);
        r = cli.Post("/api/fullscreen", "on=1", "application/x-www-form-urlencoded");
        CHECK(r && r->status == 403 && contains(r->body, "X-FeedView-Pin"));
        r = cli.Post("/api/fullscreen", httplib::Headers{{"X-FeedView-Pin", ""}}, "on=1", "application/x-www-form-urlencoded");
        CHECK(r && r->status == 200);
    }
    {
        SCENARIO("without a PIN: foreign host names are refused (DNS rebinding)");
        auto r = cli.Get("/api/state", httplib::Headers{{"Host", "evil.example.com"}});
        CHECK(r && r->status == 403);
        r = cli.Get("/api/state", httplib::Headers{{"Host", "studio-laptop.local:8080"}});
        CHECK(r && r->status == 200);
        CHECK(RemoteServer::hostAllowed("192.168.1.20:8080", "x"));
        CHECK(RemoteServer::hostAllowed("[fe80::1]:8080", "x"));
        CHECK(RemoteServer::hostAllowed("STUDIO-LAPTOP", "studio-laptop"));
        CHECK(RemoteServer::hostAllowed("feedview.lan", "x"));
        CHECK(!RemoteServer::hostAllowed("192.168.1.20.evil.com", "x"));
        CHECK(!RemoteServer::hostAllowed("localhost.evil.com:80", "x"));
    }
    {
        SCENARIO("recent clients are reported; generated PINs are 4 digits");
        auto c = srv.recentClients(30);
        CHECK(c.size() == 1 && c[0] == "127.0.0.1");
        std::string pin = RemoteServer::generatePin();
        CHECK(pin.size() == 4 && pin.find_first_not_of("0123456789") == std::string::npos);
    }
    {
        SCENARIO("a second server on the same port falls back to the next one");
        RemoteServer other;
        other.setActions({});
        const int base = port;  // occupied by srv
        int p2 = other.start(base, "127.0.0.1");
        CHECK(p2 > base && p2 <= base + 9);
        other.stop();
    }
    {
        SCENARIO("stopping with a request in flight answers it instead of hanging");
        srv.setAuth("5555", true);
        run = false;
        mainLoop.join();  // nobody processes commands now
        std::thread late([&] {
            auto r = cli.Post("/api/fullscreen", httplib::Headers{{"X-FeedView-Pin", "5555"}}, "on=1", "application/x-www-form-urlencoded");
            CHECK(r && r->status == 503);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto t0 = std::chrono::steady_clock::now();
        srv.stop();
        late.join();
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
        CHECK(!srv.running());
    }

    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
