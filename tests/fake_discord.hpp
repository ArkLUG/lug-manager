#pragma once
// A local stand-in for the Discord API (and Discord OAuth) for integration
// tests. Point the app at it with LUG_DISCORD_BASE=http://127.0.0.1:<port>;
// nothing ever reaches the real Discord. Records every request so tests can
// assert what the app would have sent.
#include <crow.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <curl/curl.h>
#include <stdexcept>

class FakeDiscord {
public:
    using json = nlohmann::json;
    struct Request { std::string method, path, body; };
    struct GuildMember { std::string id, username, global_name, nick; std::set<std::string> roles; bool bot = false; };

    std::string guild_id = "900000000000000001";
    std::map<std::string, GuildMember> members;               // by user id
    std::vector<std::pair<std::string, std::string>> roles;   // id, name
    std::vector<json> channels;
    // OAuth: the user /api/users/@me returns
    std::string oauth_user_id = "", oauth_username = "oauthuser";
    // Failure knobs
    bool members_empty = false;      // members list comes back []
    int  rate_limit_next = 0;        // answer the next N requests with 429

    // A free port picked by the OS (port 0), so parallel test binaries never collide.
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

    explicit FakeDiscord(int port = free_port()) : port_(port) {
        app_.loglevel(crow::LogLevel::Warning);
        // (CROW_CATCHALL_ROUTE doesn't get request bodies, so use a wildcard path)
        CROW_ROUTE(app_, "/<path>")
            .methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post, crow::HTTPMethod::Put,
                     crow::HTTPMethod::Patch, crow::HTTPMethod::Delete)(
            [this](const crow::request& req, const std::string&) { return handle(req); });
        future_ = app_.bindaddr("127.0.0.1").port(port_).concurrency(2).run_async();
        // Wait (bounded) until it answers - never hang a test run on a bind failure.
        bool up = false;
        for (int i = 0; i < 100 && !up; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            CURL* c = curl_easy_init();
            std::string u = "http://127.0.0.1:" + std::to_string(port_) + "/ping";
            curl_easy_setopt(c, CURLOPT_URL, u.c_str());
            curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, 1L);
            up = curl_easy_perform(c) == CURLE_OK;
            curl_easy_cleanup(c);
        }
        if (!up) throw std::runtime_error("FakeDiscord didn't start on port " + std::to_string(port_));
        clear();   // forget the startup pings
        setenv("LUG_DISCORD_BASE", ("http://127.0.0.1:" + std::to_string(port_)).c_str(), 1);
        add_member("800000000000000099", "lugbot", {}, true);
    }
    ~FakeDiscord() {
        unsetenv("LUG_DISCORD_BASE");
        app_.stop();
    }

    void add_member(const std::string& id, const std::string& username, std::set<std::string> member_roles = {},
                    bool bot = false, const std::string& nick = "") {
        std::lock_guard<std::mutex> l(mu_);
        members[id] = GuildMember{id, username, "", nick, std::move(member_roles), bot};
    }
    void remove_member(const std::string& id) { std::lock_guard<std::mutex> l(mu_); members.erase(id); }
    std::set<std::string> member_roles(const std::string& id) {
        std::lock_guard<std::mutex> l(mu_);
        auto it = members.find(id);
        return it == members.end() ? std::set<std::string>{} : it->second.roles;
    }

    std::vector<Request> requests() { std::lock_guard<std::mutex> l(mu_); return log_; }
    void clear() { std::lock_guard<std::mutex> l(mu_); log_.clear(); }
    // Requests whose "METHOD path" matches the regex.
    std::vector<Request> matching(const std::string& re) {
        std::regex r(re);
        std::vector<Request> out;
        for (const auto& q : requests())
            if (std::regex_search(q.method + " " + q.path, r)) out.push_back(q);
        return out;
    }
    // Waits up to `ms` for a matching request (for fire-and-forget calls on the worker pool).
    bool wait_for(const std::string& re, int ms = 3000) {
        for (int i = 0; i < ms / 20; ++i) {
            if (!matching(re).empty()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }

private:
    crow::response reply(int code, const json& j) {
        crow::response r(code, j.dump());
        r.set_header("Content-Type", "application/json");
        return r;
    }

    crow::response handle(const crow::request& req) {
        std::string path = req.url, method = crow::method_name(req.method);
        std::lock_guard<std::mutex> l(mu_);
        log_.push_back({method, req.raw_url, req.body});
        if (rate_limit_next > 0) {
            --rate_limit_next;
            return reply(429, {{"message", "You are being rate limited."}, {"retry_after", 0.01}, {"global", false}});
        }
        std::smatch m;
        const std::string g = "/api/v10/guilds/" + guild_id;
        auto member_json = [](const GuildMember& gm) {
            json u = {{"id", gm.id}, {"username", gm.username}, {"global_name", gm.global_name.empty() ? json(nullptr) : json(gm.global_name)}};
            if (gm.bot) u["bot"] = true;
            json roles_j = json::array();
            for (const auto& r : gm.roles) roles_j.push_back(r);
            return json{{"user", u}, {"nick", gm.nick.empty() ? json(nullptr) : json(gm.nick)}, {"roles", roles_j}};
        };

        // OAuth
        if (method == "POST" && path == "/api/oauth2/token") return reply(200, {{"access_token", "fake-access-token"}, {"token_type", "Bearer"}});
        if (method == "GET" && path == "/api/users/@me")
            return reply(200, {{"id", oauth_user_id}, {"username", oauth_username}, {"global_name", nullptr}});

        // Guild members
        if (method == "GET" && path == g + "/members") {
            json arr = json::array();
            const char* after = req.url_params.get("after");
            if (!members_empty && (!after || std::string(after) == "0"))
                for (const auto& [id, gm] : members) arr.push_back(member_json(gm));
            return reply(200, arr);
        }
        if (std::regex_match(path, m, std::regex(g + "/members/([^/]+)"))) {
            auto it = members.find(m[1]);
            if (method == "GET") {
                if (it == members.end()) return reply(404, {{"message", "Unknown Member"}, {"code", 10007}});
                return reply(200, member_json(it->second));
            }
            if (method == "PATCH" && it != members.end()) {
                auto b = json::parse(req.body, nullptr, false);
                if (b.is_object() && b.contains("nick")) it->second.nick = b["nick"].is_null() ? "" : b["nick"].get<std::string>();
                return reply(200, member_json(it->second));
            }
            if (method == "DELETE") { if (it != members.end()) members.erase(it); return crow::response(204); }
        }
        if (std::regex_match(path, m, std::regex(g + "/members/([^/]+)/roles/([^/]+)"))) {
            auto it = members.find(m[1]);
            if (it == members.end()) return reply(404, {{"message", "Unknown Member"}, {"code", 10007}});
            if (method == "PUT") it->second.roles.insert(m[2]);
            if (method == "DELETE") it->second.roles.erase(m[2]);
            return crow::response(204);
        }
        if (method == "GET" && path == g + "/roles") {
            json arr = json::array();
            for (const auto& [id, name] : roles) arr.push_back({{"id", id}, {"name", name}, {"position", 1}});
            return reply(200, arr);
        }
        if (method == "GET" && path == g + "/channels") return reply(200, channels.empty() ? json::array() : json(channels));
        if (method == "GET" && path == g + "/threads/active") return reply(200, {{"threads", json::array()}});
        if (method == "POST" && path == g + "/scheduled-events") return reply(200, {{"id", next_id()}});

        // DMs, messages, threads
        if (method == "POST" && path == "/api/v10/users/@me/channels") {
            auto b = json::parse(req.body, nullptr, false);
            std::string who = b.is_object() ? b.value("recipient_id", "") : "";
            return reply(200, {{"id", "dm" + who}, {"type", 1}});
        }
        if (method == "POST" && std::regex_match(path, m, std::regex("/api/v10/channels/([^/]+)/messages")))
            return reply(200, {{"id", next_id()}, {"channel_id", m[1].str()}});
        if (method == "POST" && std::regex_search(path, std::regex("/threads$")))
            return reply(201, {{"id", next_id()}, {"message", {{"id", next_id()}}}});
        if (method == "DELETE") return crow::response(204);
        return reply(200, {{"id", next_id()}});
    }

    std::string next_id() { return std::to_string(700000000000000000ULL + ++seq_); }

    int port_;
    crow::SimpleApp app_;
    std::future<void> future_;
    std::mutex mu_;
    std::vector<Request> log_;
    unsigned long long seq_ = 0;
};
