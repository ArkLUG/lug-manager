// Who can open what (Aaron asked for coverage, 2026-10-12).
//  - Every GET route registered in src/ must have a row in route_rules.hpp,
//    so a new page without a decided rule fails here.
//  - Each row is requested as anonymous, a member, a chapter event manager,
//    a chapter lead, a moderator and an admin; the status must match.
//  - POST/PUT/DELETE routes whose handler has no sign of an auth check are
//    listed in the output (not a failure) for review.
// Regenerate the table after an intended change:
//   ROUTE_MATRIX_PRINT=1 ./build/tests/.../test_integration_route_matrix > rows.txt
#include "integration_test_base.hpp"
#include "route_rules.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>

namespace {
struct Registered { std::string method, path, file, body; };

std::vector<Registered> scan_routes() {
    std::vector<Registered> out;
    static const std::regex route(R"re(CROW_ROUTE\(app,\s*"([^"]+)"\)\s*(\.methods\(([^)]*)\))?)re");
    static const std::regex meth(R"re("(\w+)"_method)re");
    for (const auto& f : std::filesystem::recursive_directory_iterator("src")) {
        const auto ext = f.path().extension();
        if (ext != ".cpp" && ext != ".hpp") continue;
        std::ifstream in(f.path());
        std::stringstream ss; ss << in.rdbuf();
        const std::string s = ss.str();
        std::vector<std::pair<size_t, std::smatch>> hits;
        for (auto it = std::sregex_iterator(s.begin(), s.end(), route); it != std::sregex_iterator(); ++it)
            hits.push_back({static_cast<size_t>(it->position()), *it});
        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& m = hits[i].second;
            const size_t end = i + 1 < hits.size() ? hits[i + 1].first : s.size();
            const std::string body = s.substr(hits[i].first, end - hits[i].first);
            std::string methods = m[3].matched ? m[3].str() : "\"GET\"_method";
            for (auto mt = std::sregex_iterator(methods.begin(), methods.end(), meth); mt != std::sregex_iterator(); ++mt)
                out.push_back({(*mt)[1], m[1], f.path().string(), body});
        }
    }
    return out;
}
}

class RouteMatrix : public IntegrationTest {
protected:
    std::map<std::string, std::string> ids;
    std::string moderator_token;
    void SetUp() override {
        IntegrationTest::SetUp();
        Member mod; mod.first_name = "Mo"; mod.last_name = "Derator"; mod.display_name = "Mo D."; mod.role = "moderator";
        mod.discord_user_id = "mod-test-001";
        auto mm = member_repo->create(mod);
        moderator_token = session_store->create(mm.id, "moderator", mm.display_name);
        LugEvent e; e.title = "Matrix Show"; e.start_time = "2099-05-01T09:00:00"; e.end_time = "2099-05-01T17:00:00";
        e.scope = "chapter"; e.chapter_id = test_chapter_id; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
        ids["ev"] = std::to_string(event_svc->create(e).id);
        Meeting m; m.title = "Matrix Meeting"; m.start_time = "2099-05-02T19:00:00"; m.end_time = "2099-05-02T21:00:00";
        m.scope = "chapter"; m.chapter_id = test_chapter_id; m.suppress_discord = true; m.suppress_calendar = true;
        ids["mtg"] = std::to_string(meeting_svc->create(m).id);
        ids["member"] = std::to_string(regular_member_id);
        ids["chapter"] = std::to_string(test_chapter_id);
        db->execute("INSERT INTO challenges (title, description, starts_on, ends_on) VALUES ('Matrix', '', '2099-01-01', '2099-02-01')");
        ids["challenge"] = std::to_string(query_int(*db, "SELECT MAX(id) FROM challenges"));
        db->execute("INSERT INTO perk_levels (name, year, sort_order) VALUES ('Matrix', 2099, 1)");
        ids["perk"] = std::to_string(query_int(*db, "SELECT MAX(id) FROM perk_levels"));
        db->execute("INSERT INTO treasury_entries (entry_on, kind, amount_cents) VALUES ('2099-01-01', 'income', 100)");
        ids["tx"] = std::to_string(query_int(*db, "SELECT MAX(id) FROM treasury_entries"));
        db->execute("INSERT INTO dues_payments (member_id, paid_on, amount_cents, covers_until) VALUES (" + ids["member"] +
                    ", '2099-01-01', 100, '2099-12-31')");
        ids["pay"] = std::to_string(query_int(*db, "SELECT MAX(id) FROM dues_payments"));
    }
    std::string fill(std::string p) {
        for (const auto& [k, v] : ids) {
            const std::string key = "{" + k + "}";
            for (size_t at; (at = p.find(key)) != std::string::npos;) p.replace(at, key.size(), v);
        }
        return p;
    }
    // A sample URL for a registered path (used when printing a new table)
    static std::string sample(const std::string& path) {
        std::string p = path;
        auto sub = [&](const std::string& from, const std::string& to) {
            if (p.rfind(from, 0) == 0) { p = to + p.substr(from.size()); return true; } return false;
        };
        sub("/events/<int>", "/events/{ev}") || sub("/meetings/<int>", "/meetings/{mtg}") ||
        sub("/members/<int>/dues/<int>", "/members/{member}/dues/{pay}") || sub("/members/<int>", "/members/{member}") ||
        sub("/chapters/<int>", "/chapters/{chapter}") || sub("/challenges/<int>", "/challenges/{challenge}") ||
        sub("/perks/<int>", "/perks/{perk}") || sub("/treasury/<int>", "/treasury/{tx}") ||
        sub("/attendance/member/<int>", "/attendance/member/{member}") || sub("/calendar/chapter/<int>", "/calendar/chapter/{chapter}") ||
        sub("/attendance/count/<str>/<int>", "/attendance/count/meeting/{mtg}") || sub("/attendance/list/<str>/<int>", "/attendance/list/meeting/{mtg}") ||
        sub("/settings/messages/<string>", "/settings/messages/event.announcement") || sub("/static/<path>", "/static/app.js") ||
        sub("/<path>", "/no-such-page");
        for (const char* t : {"<int>", "<string>", "<str>", "<path>"})
            for (size_t at; (at = p.find(t)) != std::string::npos;) p.replace(at, std::string(t).size(), std::string(t) == "<int>" ? "1" : "x");
        return p;
    }
    int code_as(const std::string& url, const std::string& token) { return http("GET", url, "", token).code; }
};

// Every registered GET route has a row; rows for routes that are gone fail too.
TEST_F(RouteMatrix, EveryGetRouteHasARule) {
    std::set<std::string> ruled;
    for (const auto& r : route_rules::gets) ruled.insert(r.path);
    std::set<std::string> registered;
    for (const auto& r : scan_routes()) if (r.method == "GET") registered.insert(r.path);
    ASSERT_GT(registered.size(), 100u);
    for (const auto& p : registered) EXPECT_TRUE(ruled.count(p)) << "GET " << p << " has no row in tests/support/route_rules.hpp";
    for (const auto& p : ruled) EXPECT_TRUE(registered.count(p)) << "route_rules.hpp lists GET " << p << ", which no longer exists";
}

// Changes (POST/PUT/DELETE) whose handler shows no auth check at all: listed for review.
TEST_F(RouteMatrix, ChangesWithoutAnAuthCheckAreListed) {
    static const std::regex checks(R"(require_auth|require_api_scope|\.auth\.|auth\.authenticated|can_manage|member_for|verify|token)");
    int n = 0;
    for (const auto& r : scan_routes()) {
        if (r.method == "GET") continue;
        if (std::regex_search(r.body, checks)) continue;
        // Reviewed: public on purpose, or the check is in a shared helper
        static const std::set<std::string> reviewed = {
            "/<path>",                              // the 404 page for anything else
            "/shows/<int>/interest",                // public "I'm interested" on the shows page
            "/api/v1/maintenance/discord-times",    // api lambda: require_api_scope(admin)
        };
        if (reviewed.count(r.path)) continue;
        std::cout << "[review] " << r.method << " " << r.path << " (" << r.file << ") has no visible auth check\n";
        ++n;
    }
    std::cout << "[review] " << n << " change route(s) to look at\n";
}

TEST_F(RouteMatrix, WhoCanOpenWhat) {
    const std::vector<std::pair<std::string, std::string>> roles = {
        {"anon", ""}, {"member", member_token}, {"evmgr", event_manager_token},
        {"lead", chapter_lead_token}, {"mod", moderator_token}, {"admin", admin_token}};
    if (std::getenv("ROUTE_MATRIX_PRINT")) {
        std::set<std::string> registered;
        for (const auto& r : scan_routes()) if (r.method == "GET") registered.insert(r.path);
        for (const auto& p : registered) {
            const std::string s = sample(p);
            std::cout << "    {\"" << p << "\", \"" << s << "\"";
            for (const auto& [name, tok] : roles) std::cout << ", " << code_as(fill(s), tok);
            std::cout << "},\n";
        }
        return;
    }
    for (const auto& r : route_rules::gets) {
        const std::string url = fill(r.sample);
        const int want[] = {r.anon, r.member, r.evmgr, r.lead, r.mod, r.admin};
        for (size_t i = 0; i < roles.size(); ++i)
            EXPECT_EQ(code_as(url, roles[i].second), want[i]) << "GET " << url << " as " << roles[i].first;
    }
}
