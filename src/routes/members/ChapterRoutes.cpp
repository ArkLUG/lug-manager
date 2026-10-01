#include "routes/members/ChapterRoutes.hpp"
#include <tuple>
#include <ctime>
#include <algorithm>
#include "utils/LocalTime.hpp"
#include "utils/web/ParseId.hpp"
#include "utils/AuditDiff.hpp"
#include "utils/text/HtmlEscape.hpp"
#include "utils/text/JsonEscape.hpp"
#include <crow/mustache.h>
#include <sstream>
#include <unordered_set>

// Helper: build <option> HTML for Discord role picker
static std::string build_role_options(DiscordClient& discord, const std::string& selected) {
    auto roles = discord.fetch_guild_roles();
    std::ostringstream oss;
    oss << "<option value=\"\">-- No role --</option>\n";
    for (auto& r : roles) {
        oss << "<option value=\"" << r.id << "\"";
        if (r.id == selected) oss << " selected";
        oss << ">@" << html_escape(r.name) << "</option>\n";
    }
    if (roles.empty()) {
        oss.str("");
        oss << "<option value=\"\">No roles found (configure Guild ID in Settings)</option>";
    }
    return oss.str();
}

// Helper: build <option> HTML for channel picker
static std::string build_channel_options(DiscordClient& discord, const std::string& selected) {
    auto channels = discord.fetch_text_channels();
    std::ostringstream oss;
    oss << "<option value=\"\">-- Select a channel --</option>\n";
    for (auto& ch : channels) {
        oss << "<option value=\"" << ch.id << "\"";
        if (ch.id == selected) oss << " selected";
        oss << ">#" << html_escape(ch.name) << "</option>\n";
    }
    if (channels.empty()) {
        oss.str("");
        oss << "<option value=\"\">No channels found (configure Guild ID in Settings)</option>";
    }
    return oss.str();
}

namespace {

// "Sat 10/14 7:00 PM"
std::string short_when(const std::string& iso) {
    if (iso.size() < 10) return iso;
    std::tm t{};
    if (!strptime(iso.substr(0, 10).c_str(), "%Y-%m-%d", &t)) return iso.substr(0, 10);
    t.tm_isdst = -1;
    std::mktime(&t);
    char d[24];
    std::strftime(d, sizeof(d), "%a %m/%d", &t);
    std::string out = d;
    if (out.size() > 4 && out[4] == '0') out.erase(4, 1);
    if (iso.size() >= 16) {
        int h = std::stoi(iso.substr(11, 2)), m = std::stoi(iso.substr(14, 2));
        char tb[16];
        std::snprintf(tb, sizeof(tb), " %d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h >= 12 ? "PM" : "AM");
        out += tb;
    }
    return out;
}

// Everything the chapter page shows beyond the basics: numbers for this
// year, what's coming up, recent meetings, people and Discord set-up.
void add_chapter_overview(crow::mustache::context& ctx, SqliteDatabase& db, const Chapter& ch,
                          const std::vector<ChapterMember>& people, bool can_manage) {
    const std::string now = local_iso_now(), today = now.substr(0, 10);
    const int year = local_year();
    const std::string lo = std::to_string(year) + "-01-01";
    auto num = [&](const std::string& sql, std::vector<std::string> args) -> int64_t {
        auto st = db.prepare(sql);
        st.bind(1, ch.id);
        for (size_t i = 0; i < args.size(); ++i) st.bind(static_cast<int>(i + 2), args[i]);
        return st.step() ? st.col_int(0) : 0;
    };
    int64_t held = num("SELECT COUNT(*) FROM meetings WHERE chapter_id=? AND status<>'cancelled' AND start_time>=? AND start_time<?", {lo, now});
    int64_t checkins = num("SELECT COUNT(*) FROM attendance a JOIN meetings m ON m.id=a.entity_id AND a.entity_type='meeting' "
                           "WHERE m.chapter_id=? AND m.status<>'cancelled' AND m.start_time>=? AND m.start_time<?", {lo, now});
    int64_t events = num("SELECT COUNT(*) FROM lug_events WHERE chapter_id=? AND status<>'cancelled' AND start_time>=? AND start_time<?",
                         {lo, std::to_string(year + 1) + "-01-01"});
    int64_t active = num("SELECT COUNT(*) FROM (SELECT a.member_id FROM attendance a JOIN meetings m ON m.id=a.entity_id AND a.entity_type='meeting' "
                         " WHERE m.chapter_id=?1 AND m.start_time>=date(?2,'-90 days') "
                         " UNION SELECT eda.member_id FROM event_day_attendance eda JOIN event_days d ON d.id=eda.event_day_id "
                         " JOIN lug_events e ON e.id=d.event_id WHERE e.chapter_id=?1 AND d.day_date>=date(?2,'-90 days'))", {today});
    int organisers = 0, member_count = 0;
    crow::json::wvalue managers = crow::json::wvalue::list(), everyone = crow::json::wvalue::list();
    int mi = 0, ei = 0;
    for (const auto& p : people) {
        ++member_count;
        if (p.chapter_role == "lead" || p.chapter_role == "event_manager") ++organisers;
        if (p.chapter_role == "event_manager") { managers[mi]["name"] = p.display_name; ++mi; }
        if (ei < 60) { everyone[ei]["name"] = p.display_name; everyone[ei]["lead"] = p.chapter_role == "lead";
                       everyone[ei]["manager"] = p.chapter_role == "event_manager"; ++ei; }
    }
    ctx["member_count"] = member_count;
    ctx["organiser_count"] = organisers;
    ctx["meetings_held"] = held;
    char avg[16];
    std::snprintf(avg, sizeof(avg), "%.1f", held ? static_cast<double>(checkins) / held : 0.0);
    ctx["avg_attendance"] = std::string(avg);
    ctx["events_this_year"] = events;
    ctx["active_90"] = active;
    ctx["year"] = year;
    std::string mgr_names;
    for (const auto& p : people) if (p.chapter_role == "event_manager") mgr_names += (mgr_names.empty() ? "" : ", ") + p.display_name;
    ctx["event_manager_names"] = mgr_names;
    ctx["has_event_managers"] = mi > 0;
    (void)managers;
    ctx["people"] = std::move(everyone);
    ctx["has_people"] = ei > 0;
    ctx["more_people"] = member_count > 60 ? member_count - 60 : 0;

    // Coming up: this chapter's meetings and events, soonest first
    struct Item { std::string when, kind, title, place; int64_t id; bool tentative; };
    std::vector<Item> up;
    {
        auto st = db.prepare("SELECT id, start_time, title, location, status FROM meetings WHERE chapter_id=? AND status<>'cancelled' "
                             "AND start_time>=? ORDER BY start_time LIMIT 6");
        st.bind(1, ch.id); st.bind(2, today);
        while (st.step()) up.push_back({st.col_text(1), "meeting", st.col_text(2), st.col_text(3), st.col_int(0), false});
    }
    {
        auto st = db.prepare("SELECT id, start_time, title, location, status FROM lug_events WHERE chapter_id=? AND status<>'cancelled' "
                             "AND COALESCE(NULLIF(end_time,''), start_time)>=? ORDER BY start_time LIMIT 6");
        st.bind(1, ch.id); st.bind(2, today);
        while (st.step()) up.push_back({st.col_text(1), "event", st.col_text(2), st.col_text(3), st.col_int(0), st.col_text(4) == "tentative"});
    }
    std::sort(up.begin(), up.end(), [](const Item& a, const Item& b) { return a.when < b.when; });
    if (up.size() > 6) up.resize(6);
    crow::json::wvalue upcoming = crow::json::wvalue::list();
    for (size_t i = 0; i < up.size(); ++i) {
        upcoming[i]["when"] = short_when(up[i].when);
        upcoming[i]["title"] = up[i].title;
        upcoming[i]["place"] = up[i].place;
        upcoming[i]["url"] = "/" + up[i].kind + "s/" + std::to_string(up[i].id);
        upcoming[i]["is_event"] = up[i].kind == "event";
        upcoming[i]["tentative"] = up[i].tentative;
    }
    ctx["upcoming"] = std::move(upcoming);
    ctx["has_upcoming"] = !up.empty();

    // Recent meetings with how many came
    {
        auto st = db.prepare("SELECT m.id, m.start_time, m.title, (SELECT COUNT(*) FROM attendance a WHERE a.entity_type='meeting' AND a.entity_id=m.id) "
                             "FROM meetings m WHERE m.chapter_id=? AND m.status<>'cancelled' AND m.start_time<? ORDER BY m.start_time DESC LIMIT 5");
        st.bind(1, ch.id); st.bind(2, now);
        crow::json::wvalue recent = crow::json::wvalue::list();
        int i = 0;
        int64_t peak = 1;
        std::vector<std::tuple<int64_t, std::string, std::string, int64_t>> rows;
        while (st.step()) { rows.emplace_back(st.col_int(0), st.col_text(1), st.col_text(2), st.col_int(3)); peak = std::max(peak, st.col_int(3)); }
        for (const auto& [id, when, title, n] : rows) {
            recent[i]["url"] = "/meetings/" + std::to_string(id);
            recent[i]["when"] = short_when(when);
            recent[i]["title"] = title;
            recent[i]["count"] = n;
            recent[i]["pct"] = static_cast<int>(n * 100 / peak);
            ++i;
        }
        ctx["recent"] = std::move(recent);
        ctx["has_recent"] = i > 0;
    }

    // Most active this year
    {
        auto st = db.prepare("SELECT mb.display_name, COUNT(*) AS n FROM ("
                             " SELECT a.member_id AS mid FROM attendance a JOIN meetings m ON m.id=a.entity_id AND a.entity_type='meeting' "
                             "  WHERE m.chapter_id=?1 AND m.start_time>=?2 "
                             " UNION ALL SELECT eda.member_id FROM event_day_attendance eda JOIN event_days d ON d.id=eda.event_day_id "
                             "  JOIN lug_events e ON e.id=d.event_id WHERE e.chapter_id=?1 AND d.day_date>=?2) x "
                             "JOIN members mb ON mb.id=x.mid GROUP BY x.mid ORDER BY n DESC, mb.display_name LIMIT 5");
        st.bind(1, ch.id); st.bind(2, lo);
        crow::json::wvalue top = crow::json::wvalue::list();
        int i = 0;
        while (st.step()) { top[i]["name"] = st.col_text(0); top[i]["count"] = st.col_int(1); ++i; }
        ctx["top"] = std::move(top);
        ctx["has_top"] = i > 0;
    }

    // Discord set-up (managers)
    ctx["show_discord_status"] = can_manage;
    ctx["has_discord_channel"] = !ch.discord_announcement_channel_id.empty();
    ctx["has_lead_role"] = !ch.discord_lead_role_id.empty();
    ctx["has_member_role"] = !ch.discord_member_role_id.empty();
}

} // namespace

void register_chapter_routes(LugApp& app, ChapterService& chapters,
                              ChapterMemberRepository& chapter_members,
                              MemberService& members,
                              DiscordClient& discord, AuditService& audit) {

    // GET /chapters - list all chapters
    CROW_ROUTE(app, "/chapters")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) {
            res.code = 401;
            res.write("Unauthorized");
            return res;
        }

        auto all_chapters = chapters.list_all();
        auto stats = chapter_members.get_all_chapter_stats();

        crow::mustache::context mctx;
        mctx["title"] = "Chapters";
        mctx["is_admin"] = ctx.auth.is_admin();
        mctx["can_see_dues"] = ctx.auth.is_chapter_lead();

        bool can_see_dues = ctx.auth.is_chapter_lead();
        crow::json::wvalue arr;
        for (size_t i = 0; i < all_chapters.size(); ++i) {
            const auto& ch = all_chapters[i];
            arr[i]["id"]          = ch.id;
            arr[i]["name"]        = ch.name;
            arr[i]["can_see_dues"] = can_see_dues;
            arr[i]["shorthand"]   = ch.shorthand;
            arr[i]["has_shorthand"] = !ch.shorthand.empty();
            arr[i]["description"] = ch.description;

            auto it = stats.find(ch.id);
            if (it != stats.end()) {
                const auto& s = it->second;
                arr[i]["member_count"] = s.member_count;
                arr[i]["paid_count"]   = s.paid_count;
                // Join lead names into a comma-separated string
                std::string leads_str;
                for (size_t j = 0; j < s.lead_names.size(); ++j) {
                    if (j > 0) leads_str += ", ";
                    leads_str += s.lead_names[j];
                }
                arr[i]["leads_str"]    = leads_str;
                arr[i]["has_leads"]    = !s.lead_names.empty();
            } else {
                arr[i]["member_count"] = 0;
                arr[i]["paid_count"]   = 0;
                arr[i]["leads_str"]    = "";
                arr[i]["has_leads"]    = false;
            }
        }
        mctx["chapters"] = std::move(arr);

        return html_page(req, app, crow::mustache::load("chapters/_list.html").render(mctx).dump(), "Chapters", "active_chapters");
    });

    // POST /chapters - create chapter (admin only)
    CROW_ROUTE(app, "/chapters").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!require_auth(req, res, app, "admin")) return res;

        auto params = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        Chapter ch;
        ch.name = get_param("name");
        ch.shorthand = get_param("shorthand");
        ch.description = get_param("description");
        ch.discord_announcement_channel_id = get_param("discord_announcement_channel_id");
        ch.discord_lead_role_id            = get_param("discord_lead_role_id");
        ch.discord_member_role_id          = get_param("discord_member_role_id");
        ch.created_by = ctx.auth.member_id;

        try {
            chapters.create(ch);
            audit.log(req, app, "chapter.create", "chapter", ch.id, ch.name, "Created chapter");
            res.add_header("HX-Redirect", "/chapters");
            res.code = 200;
            res.write("{\"success\":true}");
            res.add_header("Content-Type", "application/json");
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string("{\"error\":\"") + json_escape(e.what()) + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // GET /chapters/<id> - chapter detail
    CROW_ROUTE(app, "/chapters/<int>")([&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) {
            res.code = 401;
            res.write("Unauthorized");
            return res;
        }

        auto ch = chapters.get(static_cast<int64_t>(id));
        if (!ch) {
            res.code = 404;
            res.write("Chapter not found");
            return res;
        }

        bool is_admin = ctx.auth.role == "admin";
        bool can_manage = is_admin;
        if (!can_manage) {
            auto role = chapter_members.get_chapter_role(ctx.auth.member_id, ch->id);
            can_manage = (role && *role == "lead");
        }

        // Build leads list and "add lead" options (non-leads in chapter)
        auto ch_members = chapter_members.find_by_chapter(ch->id);
        crow::json::wvalue leads_arr;
        std::ostringstream add_lead_opts;
        size_t lead_count = 0;
        bool has_non_leads = false;
        add_lead_opts << "<option value=\"\">-- Select member --</option>\n";
        for (auto& cm : ch_members) {
            if (cm.chapter_role == "lead") {
                leads_arr[lead_count]["member_id"]       = cm.member_id;
                leads_arr[lead_count]["display_name"]    = cm.display_name;
                leads_arr[lead_count]["discord_username"]= cm.discord_username;
                leads_arr[lead_count]["has_discord_username"]= !cm.discord_username.empty();
                leads_arr[lead_count]["chapter_id"]      = ch->id;
                ++lead_count;
            } else {
                add_lead_opts << "<option value=\"" << cm.member_id << "\">"
                              << html_escape(cm.display_name);
                if (!cm.discord_username.empty())
                    add_lead_opts << " (@" << html_escape(cm.discord_username) << ")";
                add_lead_opts << "</option>\n";
                has_non_leads = true;
            }
        }

        crow::mustache::context mctx;
        mctx["id"]              = ch->id;
        mctx["name"]            = ch->name;
        mctx["shorthand"]       = ch->shorthand;
        mctx["has_shorthand"]   = !ch->shorthand.empty();
        mctx["description"]     = ch->description;
        mctx["discord_channel"] = ch->discord_announcement_channel_id;
        mctx["is_admin"]        = is_admin;
        mctx["can_manage"]      = can_manage;
        mctx["leads"]           = std::move(leads_arr);
        mctx["has_leads"]       = lead_count > 0;
        mctx["add_lead_options"]= add_lead_opts.str();
        mctx["has_non_leads"]   = has_non_leads;
        add_chapter_overview(mctx, members.repo().db(), *ch, ch_members, can_manage);

        res.add_header("Content-Type", "text/html; charset=utf-8");
        return html_page(req, app, crow::mustache::load("chapters/_detail.html").render(mctx).dump(), ch->name, "active_chapters");
    });

    // POST /chapters/<id>/lead - add a chapter lead (admin only)
    CROW_ROUTE(app, "/chapters/<int>/lead").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req);

        int64_t chapter_id = static_cast<int64_t>(id);

        auto params = crow::query_string("?" + req.body);
        const char* mid_raw = params.get("member_id");
        if (!mid_raw || std::string(mid_raw).empty()) {
            res.code = 400; res.write("member_id required"); return res;
        }

        int64_t new_lead_id = parse_id(mid_raw);
        chapter_members.upsert(new_lead_id, chapter_id, "lead", ctx.auth.member_id);

        // Assign the chapter's lead Discord role if configured
        auto ch = chapters.get(chapter_id);
        auto lead_member = members.get(new_lead_id);
        std::string ch_name = ch ? ch->name : "";
        std::string lead_name = lead_member ? lead_member->display_name : std::to_string(new_lead_id);
        audit.log(req, app, "chapter.lead_assign", "chapter", chapter_id, ch_name, "Assigned lead: " + lead_name);
        if (ch && !ch->discord_lead_role_id.empty()) {
            if (lead_member && !lead_member->discord_user_id.empty()) {
                try {
                    discord.add_member_role(lead_member->discord_user_id, ch->discord_lead_role_id);
                } catch (const std::exception& e) {
                    std::cerr << "[ChapterRoutes] Failed to add lead role: " << e.what() << "\n";
                }
            }
        }

        res.add_header("HX-Redirect", "/chapters/" + std::to_string(id));
        res.code = 200;
        return res;
    });

    // POST /chapters/<id>/lead/<member_id>/demote - remove lead role (admin only)
    CROW_ROUTE(app, "/chapters/<int>/lead/<int>/demote").methods("POST"_method)(
        [&](const crow::request& req, int id, int member_id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req);

        int64_t chapter_id = static_cast<int64_t>(id);

        chapter_members.upsert(static_cast<int64_t>(member_id), chapter_id,
                               "member", ctx.auth.member_id);

        // Remove the chapter's lead Discord role if configured
        auto ch = chapters.get(chapter_id);
        auto m = members.get(static_cast<int64_t>(member_id));
        std::string ch_name = ch ? ch->name : "";
        std::string demoted_name = m ? m->display_name : std::to_string(member_id);
        audit.log(req, app, "chapter.lead_demote", "chapter", chapter_id, ch_name, "Demoted lead: " + demoted_name);
        if (ch && !ch->discord_lead_role_id.empty()) {
            if (m && !m->discord_user_id.empty()) {
                try {
                    discord.remove_member_role(m->discord_user_id, ch->discord_lead_role_id);
                } catch (const std::exception& e) {
                    std::cerr << "[ChapterRoutes] Failed to remove lead role: " << e.what() << "\n";
                }
            }
        }

        res.add_header("HX-Redirect", "/chapters/" + std::to_string(id));
        res.code = 200;
        return res;
    });

    // PUT /chapters/<id> - update chapter (admin only)
    CROW_ROUTE(app, "/chapters/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto ch_before = chapters.get(static_cast<int64_t>(id));

        // Support both form-encoded (from modal) and JSON (from API clients)
        Chapter updates;
        std::string content_type = req.get_header_value("Content-Type");
        if (content_type.find("application/json") != std::string::npos) {
            auto body = crow::json::load(req.body);
            if (!body) {
                res.code = 400;
                res.write("{\"error\":\"Invalid JSON\"}");
                res.add_header("Content-Type", "application/json");
                return res;
            }
            if (body.has("name")) updates.name = body["name"].s();
            if (body.has("shorthand")) updates.shorthand = body["shorthand"].s();
            if (body.has("description")) updates.description = body["description"].s();
            if (body.has("discord_announcement_channel_id"))
                updates.discord_announcement_channel_id = body["discord_announcement_channel_id"].s();
            if (body.has("discord_lead_role_id"))
                updates.discord_lead_role_id = body["discord_lead_role_id"].s();
            if (body.has("discord_member_role_id"))
                updates.discord_member_role_id = body["discord_member_role_id"].s();
        } else {
            auto params = crow::query_string("?" + req.body);
            auto gp = [&](const char* k) -> std::string {
                const char* v = params.get(k); return v ? std::string(v) : "";
            };
            updates.name = gp("name");
            updates.shorthand = gp("shorthand");
            updates.description = gp("description");
            updates.discord_announcement_channel_id = gp("discord_announcement_channel_id");
            updates.discord_lead_role_id   = gp("discord_lead_role_id");
            updates.discord_member_role_id = gp("discord_member_role_id");
        }

        try {
            auto updated = chapters.update(static_cast<int64_t>(id), updates);
            {
                AuditDiff diff;
                if (ch_before) {
                    diff.field("name", ch_before->name, updated.name);
                    diff.field("shorthand", ch_before->shorthand, updated.shorthand);
                    diff.field("description", ch_before->description, updated.description);
                    diff.field("discord_announcement_channel_id", ch_before->discord_announcement_channel_id, updated.discord_announcement_channel_id);
                    diff.field("discord_lead_role_id", ch_before->discord_lead_role_id, updated.discord_lead_role_id);
                    diff.field("discord_member_role_id", ch_before->discord_member_role_id, updated.discord_member_role_id);
                }
                audit.log(req, app, "chapter.update", "chapter", static_cast<int64_t>(id), updated.name,
                          diff.has_changes() ? diff.str() : "No field changes");
            }
            res.write("{\"success\":true}");
            res.add_header("Content-Type", "application/json");
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string("{\"error\":\"") + json_escape(e.what()) + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // DELETE /chapters/<id> - delete chapter (admin only)
    CROW_ROUTE(app, "/chapters/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        try {
            auto ch_del = chapters.get(static_cast<int64_t>(id));
            std::string del_name = ch_del ? ch_del->name : "";
            chapters.delete_chapter(static_cast<int64_t>(id));
            audit.log(req, app, "chapter.delete", "chapter", static_cast<int64_t>(id), del_name, "Deleted chapter");
            res.add_header("HX-Redirect", "/chapters");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string("{\"error\":\"") + json_escape(e.what()) + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // GET /chapters/new - create form modal (admin only)
    CROW_ROUTE(app, "/chapters/new")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        crow::mustache::context mctx;
        mctx["channel_options"]      = build_channel_options(discord, "");
        mctx["role_options"]         = build_role_options(discord, "");
        mctx["member_role_options"]  = build_role_options(discord, "");

        auto tmpl = crow::mustache::load("chapters/_form.html");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /chapters/<id>/members - chapter member management (admin or chapter lead)
    CROW_ROUTE(app, "/chapters/<int>/members")([&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; res.write("Unauthorized"); return res; }

        int64_t chapter_id = static_cast<int64_t>(id);
        bool is_admin = ctx.auth.role == "admin";
        bool is_lead  = false;
        if (!is_admin) {
            auto role = chapter_members.get_chapter_role(ctx.auth.member_id, chapter_id);
            is_lead = (role && *role == "lead");
        }
        if (!is_admin && !is_lead) {
            res.code = 403; res.write("Forbidden"); return res;
        }

        auto ch = chapters.get(chapter_id);
        if (!ch) { res.code = 404; res.write("Chapter not found"); return res; }

        auto member_list = chapter_members.find_by_chapter(chapter_id);
        // All LUG members not already in this chapter (for the "add member" dropdown)
        auto all_members = members.list_all();

        crow::mustache::context mctx;
        mctx["chapter_id"]   = ch->id;
        mctx["chapter_name"] = ch->name;
        mctx["is_admin"]     = is_admin;

        crow::json::wvalue cm_arr;
        for (size_t i = 0; i < member_list.size(); ++i) {
            const auto& cm = member_list[i];
            cm_arr[i]["member_id"]       = cm.member_id;
            cm_arr[i]["display_name"]    = cm.display_name;
            cm_arr[i]["discord_username"]= cm.discord_username;
            cm_arr[i]["has_discord_username"]= !cm.discord_username.empty();
            cm_arr[i]["chapter_role"]    = cm.chapter_role;
            cm_arr[i]["is_lead"]         = (cm.chapter_role == "lead");
            cm_arr[i]["is_event_manager"]= (cm.chapter_role == "event_manager");
            cm_arr[i]["is_member"]       = (cm.chapter_role == "member");
        }
        mctx["members"] = std::move(cm_arr);

        // Build set of existing member IDs for filtering
        std::unordered_set<int64_t> in_chapter;
        for (auto& cm : member_list) in_chapter.insert(cm.member_id);

        std::ostringstream member_opts;
        member_opts << "<option value=\"\">-- Select member --</option>\n";
        for (auto& m : all_members) {
            if (in_chapter.count(m.id)) continue;
            member_opts << "<option value=\"" << m.id << "\">"
                        << html_escape(m.display_name);
            if (!m.discord_username.empty())
                member_opts << " (@" << html_escape(m.discord_username) << ")";
            member_opts << "</option>\n";
        }
        mctx["member_options"] = member_opts.str();

        return html_page(req, app, crow::mustache::load("chapters/_members.html").render(mctx).dump(),
                         ch->name + " — Members", "active_chapters");
    });

    // POST /chapters/<id>/members - add or update a member's chapter role
    CROW_ROUTE(app, "/chapters/<int>/members").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; return res; }

        int64_t chapter_id = static_cast<int64_t>(id);
        bool is_admin = ctx.auth.role == "admin";
        if (!is_admin) {
            auto role = chapter_members.get_chapter_role(ctx.auth.member_id, chapter_id);
            if (!role || *role != "lead") {
                res.code = 403;
                res.write(R"(<div class="text-red-500 text-sm p-2">Only chapter leads can manage members.</div>)");
                return res;
            }
        }

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };

        std::string member_id_str = gp("member_id");
        std::string chapter_role  = gp("chapter_role");
        if (member_id_str.empty() || chapter_role.empty()) {
            res.code = 400;
            res.write(R"(<div class="text-red-500 text-sm p-2">member_id and chapter_role required.</div>)");
            return res;
        }

        int64_t member_id = parse_id(member_id_str);
        if (member_id == 0) { res.code = 400; return res; }

        // Only admins can assign lead role
        if (chapter_role == "lead" && !is_admin) {
            res.code = 403;
            res.write(R"(<div class="text-red-500 text-sm p-2">Only admins can assign the lead role.</div>)");
            return res;
        }

        // Check previous role so we know whether to add/remove the Discord lead role
        if (chapter_role != "lead" && chapter_role != "event_manager" && chapter_role != "member") {
            res.code = 400;
            res.write(R"(<div class="text-red-500 text-sm p-2">Invalid chapter role.</div>)");
            return res;
        }

        auto prev_role = chapter_members.get_chapter_role(member_id, chapter_id);
        bool was_lead  = prev_role && *prev_role == "lead";
        bool will_lead = chapter_role == "lead";

        // Leads are appointed by admins only, so only admins may demote them too.
        if (was_lead && !will_lead && !is_admin) {
            res.code = 403;
            res.write(R"(<div class="text-red-500 text-sm p-2">Only admins can change a chapter lead's role.</div>)");
            return res;
        }

        chapter_members.upsert(member_id, chapter_id, chapter_role, ctx.auth.member_id);

        // Sync Discord lead role if the chapter has one configured
        auto ch = chapters.get(chapter_id);
        auto mbr = members.get(member_id);
        std::string ch_name = ch ? ch->name : "";
        std::string mbr_name = mbr ? mbr->display_name : member_id_str;

        if (!prev_role) {
            audit.log(req, app, "chapter.member_add", "chapter", chapter_id, ch_name, "Added member " + mbr_name);
        } else if (*prev_role != chapter_role) {
            audit.log(req, app, "chapter.member_role", "chapter", chapter_id, ch_name, "Changed " + mbr_name + " role to " + chapter_role);
        }

        if (ch && !ch->discord_lead_role_id.empty()) {
            auto member = members.get(member_id);
            if (member && !member->discord_user_id.empty()) {
                if (!was_lead && will_lead) {
                    discord.add_member_role(member->discord_user_id, ch->discord_lead_role_id);
                } else if (was_lead && !will_lead) {
                    discord.remove_member_role(member->discord_user_id, ch->discord_lead_role_id);
                }
            }
        }

        res.add_header("HX-Redirect", "/chapters/" + std::to_string(id) + "/members");
        res.code = 200;
        return res;
    });

    // DELETE /chapters/<id>/members/<member_id> - remove member from chapter
    CROW_ROUTE(app, "/chapters/<int>/members/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id, int member_id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; return res; }

        int64_t chapter_id = static_cast<int64_t>(id);
        bool is_admin = ctx.auth.role == "admin";
        if (!is_admin) {
            auto role = chapter_members.get_chapter_role(ctx.auth.member_id, chapter_id);
            if (!role || *role != "lead") { res.code = 403; return res; }
        }

        // Remove Discord lead role if they had it
        auto prev_role = chapter_members.get_chapter_role(static_cast<int64_t>(member_id), chapter_id);
        // Leads are appointed by admins only, so only admins may remove them too.
        if (prev_role && *prev_role == "lead" && !is_admin) {
            res.code = 403;
            return res;
        }
        if (prev_role && *prev_role == "lead") {
            auto ch = chapters.get(chapter_id);
            if (ch && !ch->discord_lead_role_id.empty()) {
                auto member = members.get(static_cast<int64_t>(member_id));
                if (member && !member->discord_user_id.empty()) {
                    discord.remove_member_role(member->discord_user_id, ch->discord_lead_role_id);
                }
            }
        }

        auto ch_rm = chapters.get(chapter_id);
        auto mbr_rm = members.get(static_cast<int64_t>(member_id));
        std::string ch_rm_name = ch_rm ? ch_rm->name : "";
        std::string mbr_rm_name = mbr_rm ? mbr_rm->display_name : std::to_string(member_id);
        chapter_members.remove(static_cast<int64_t>(member_id), chapter_id);
        audit.log(req, app, "chapter.member_remove", "chapter", chapter_id, ch_rm_name, "Removed member " + mbr_rm_name);
        res.add_header("HX-Redirect", "/chapters/" + std::to_string(id) + "/members");
        res.code = 200;
        return res;
    });

    // GET /chapters/<id>/edit - edit form modal (admin only)
    CROW_ROUTE(app, "/chapters/<int>/edit")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto ch = chapters.get(static_cast<int64_t>(id));
        if (!ch) {
            res.code = 404;
            res.write("Chapter not found");
            return res;
        }

        crow::mustache::context mctx;
        mctx["id"]              = ch->id;
        mctx["name"]            = ch->name;
        mctx["shorthand"]       = ch->shorthand;
        mctx["description"]     = ch->description;
        mctx["channel_options"]     = build_channel_options(discord, ch->discord_announcement_channel_id);
        mctx["role_options"]        = build_role_options(discord, ch->discord_lead_role_id);
        mctx["member_role_options"] = build_role_options(discord, ch->discord_member_role_id);
        mctx["is_edit"]             = true;

        auto tmpl = crow::mustache::load("chapters/_form.html");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(tmpl.render(mctx).dump());
        return res;
    });
}
