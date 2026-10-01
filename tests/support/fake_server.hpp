#pragma once
// Base for local stand-ins of external APIs (Discord, Google) in tests:
// listens on 127.0.0.1 at an OS-picked port, records every request, and
// answers through a subclass's handle(). Nothing reaches a real service.
#include <crow.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class FakeServer {
public:
    struct Request { std::string method, path, body; };

    virtual ~FakeServer() { stop(); }

    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }
    std::vector<Request> requests() { std::lock_guard<std::mutex> l(mu_); return log_; }
    void clear() { std::lock_guard<std::mutex> l(mu_); log_.clear(); }
    // Requests whose "METHOD path?query" matches the regex.
    std::vector<Request> matching(const std::string& re) {
        std::regex r(re);
        std::vector<Request> out;
        for (const auto& q : requests())
            if (std::regex_search(q.method + " " + q.path, r)) out.push_back(q);
        return out;
    }
    // Waits up to `ms` for a matching request (fire-and-forget calls on worker pools).
    bool wait_for(const std::string& re, int ms = 3000) {
        for (int i = 0; i < ms / 20; ++i) {
            if (!matching(re).empty()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }

protected:
    FakeServer() : port_(free_port()) {}

    // Call at the end of the subclass constructor.
    void start() {
        app_.loglevel(crow::LogLevel::Warning);
        // (CROW_CATCHALL_ROUTE doesn't get request bodies, so use a wildcard path)
        CROW_ROUTE(app_, "/<path>")
            .methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post, crow::HTTPMethod::Put,
                     crow::HTTPMethod::Patch, crow::HTTPMethod::Delete)(
            [this](const crow::request& req, const std::string&) {
                std::lock_guard<std::mutex> l(mu_);
                log_.push_back({crow::method_name(req.method), req.raw_url, req.body});
                return handle(req);
            });
        future_ = app_.bindaddr("127.0.0.1").port(port_).concurrency(2).run_async();
        // Bounded wait until it answers - never hang a test run on a bind failure.
        bool up = false;
        for (int i = 0; i < 100 && !up; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            CURL* c = curl_easy_init();
            std::string u = base_url() + "/ping";
            curl_easy_setopt(c, CURLOPT_URL, u.c_str());
            curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, 1L);
            up = curl_easy_perform(c) == CURLE_OK;
            curl_easy_cleanup(c);
        }
        if (!up) throw std::runtime_error("fake server didn't start on port " + std::to_string(port_));
        clear();   // forget the startup pings
    }

    // Subclasses call this first in their destructor, so no request is
    // handled while their state is being torn down.
    void stop() {
        if (stopped_) return;
        stopped_ = true;
        app_.stop();
    }

    // Called with the request-log mutex held.
    virtual crow::response handle(const crow::request& req) = 0;

    static crow::response json_reply(int code, const std::string& body) {
        crow::response r(code, body);
        r.set_header("Content-Type", "application/json");
        return r;
    }

private:
    static int free_port() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        socklen_t len = sizeof(a);
        int p = 0;
        if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
            getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len) == 0) p = ntohs(a.sin_port);
        close(fd);
        return p;
    }

    int port_;
    crow::SimpleApp app_;
    std::future<void> future_;
    std::mutex mu_;
    std::vector<Request> log_;
    bool stopped_ = false;
};
