#pragma once
// A local stand-in for the Discord API (and Discord OAuth) for integration
// tests. Constructing one points the app at it (LUG_DISCORD_BASE); nothing
// ever reaches the real Discord. Records every request so tests can assert
// what the app would have sent.
#include "fake_server.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <set>

class FakeDiscord : public FakeServer {
public:
    using json = nlohmann::json;
    struct GuildMember { std::string id, username, global_name, nick; std::set<std::string> roles; bool bot = false; };

    std::string guild_id = "900000000000000001";
    std::map<std::string, GuildMember> members;               // by user id
    std::vector<std::pair<std::string, std::string>> roles;   // id, name
    std::vector<json> channels;
    // OAuth: the user /api/users/@me returns
    std::string oauth_user_id = "", oauth_username = "oauthuser";
    std::string oauth_email = "";      // with the email scope
    bool oauth_verified = true;
    // Scheduled events and channel messages the fake remembers (GET returns
    // them, PATCH updates them). Keyed by event id / "channel/message".
    std::map<std::string, json> scheduled_events;
    std::map<std::string, json> messages;
    // Failure knobs
    bool members_empty = false;      // members list comes back []
    int  rate_limit_next = 0;        // answer the next N requests with 429
    bool refuse_edits = false;       // PATCH of scheduled events / messages -> 400

    FakeDiscord() {
        start();
        setenv("LUG_DISCORD_BASE", base_url().c_str(), 1);
        add_member("800000000000000099", "lugbot", {}, true);
    }
    ~FakeDiscord() override { stop(); unsetenv("LUG_DISCORD_BASE"); }

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

protected:
    crow::response reply(int code, const json& j) { return json_reply(code, j.dump()); }

    crow::response handle(const crow::request& req) override {
        std::string path = req.url, method = crow::method_name(req.method);
        std::lock_guard<std::mutex> state_lock(mu_);
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
            return reply(200, {{"id", oauth_user_id}, {"username", oauth_username}, {"global_name", nullptr},
                               {"email", oauth_email.empty() ? json(nullptr) : json(oauth_email)}, {"verified", oauth_verified}});

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
        if (std::regex_match(path, m, std::regex(g + "/scheduled-events/([^/?]+)"))) {
            auto it = scheduled_events.find(m[1]);
            if (method == "PATCH" && refuse_edits) return reply(400, {{"message", "Invalid Form Body"}, {"code", 50035}});
            if (method == "GET" || method == "PATCH") {
                if (it == scheduled_events.end()) {
                    if (method == "PATCH") return reply(200, {{"id", m[1].str()}});
                    return reply(404, {{"message", "Unknown Guild Scheduled Event"}, {"code", 10070}});
                }
                if (method == "PATCH") {
                    auto b = json::parse(req.body, nullptr, false);
                    if (b.is_object()) for (auto& [k, v] : b.items()) it->second[k] = v;
                }
                return reply(200, it->second);
            }
        }
        if (std::regex_match(path, m, std::regex("/api/v10/channels/([^/]+)/messages/([^/?]+)"))) {
            auto it = messages.find(m[1].str() + "/" + m[2].str());
            if (method == "PATCH" && refuse_edits) return reply(400, {{"message", "Invalid Form Body"}, {"code", 50035}});
            if (method == "GET" || method == "PATCH") {
                if (it == messages.end()) {
                    if (method == "PATCH") return reply(200, {{"id", m[2].str()}});
                    return reply(404, {{"message", "Unknown Message"}, {"code", 10008}});
                }
                if (method == "PATCH") {
                    auto b = json::parse(req.body, nullptr, false);
                    if (b.is_object() && b.contains("content")) it->second["content"] = b["content"];
                }
                return reply(200, it->second);
            }
        }

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
    std::mutex mu_;   // guards the member/role state the tests touch
    unsigned long long seq_ = 0;
};
