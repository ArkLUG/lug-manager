#include "routes/PerkRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <sstream>
#include <ctime>

// Compute the highest perk level a member qualifies for.
// Returns nullopt if they don't qualify for any.
static std::optional<PerkLevel> compute_perk_level(
        const std::vector<PerkLevel>& levels,
        int meeting_count, int event_count,
        bool is_paid, const std::string& member_fol) {
    std::optional<PerkLevel> best;
    for (const auto& lvl : levels) {
        if (meeting_count >= lvl.meeting_attendance_required &&
            event_count   >= lvl.event_attendance_required &&
            (!lvl.requires_paid_dues || is_paid) &&
            fol_rank(member_fol) >= fol_rank(lvl.min_fol_status)) {
            if (!best || lvl.sort_order > best->sort_order) {
                best = lvl;
            }
        }
    }
    return best;
}

void register_perk_routes(LugApp& app, PerkLevelRepository& perks,
                           AttendanceRepository& attendance,
                           MemberRepository& members,
                           DiscordClient& discord,
                           AuditService& audit) {

    // GET /settings/perks — list perk levels for a year (admin only)
    CROW_ROUTE(app, "/settings/perks")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        // Determine selected year
        std::time_t now_t = std::time(nullptr);
        std::tm* tm_now = std::localtime(&now_t);
        int current_year = tm_now->tm_year + 1900;
        int selected_year = current_year;
        {
            auto qs = crow::query_string(req.url_params);
            const char* y = qs.get("year");
            if (y) try { selected_year = std::stoi(y); } catch (...) {}
        }

        auto levels = perks.find_by_year(selected_year);

        // Year dropdown
        auto perk_years = perks.get_perk_years();
        if (std::find(perk_years.begin(), perk_years.end(), current_year) == perk_years.end())
            perk_years.insert(perk_years.begin(), current_year);

        // Build role ID -> name lookup for display
        std::map<std::string, std::string> role_names;
        try {
            auto guild_roles = discord.fetch_guild_roles();
            for (const auto& r : guild_roles) role_names[r.id] = r.name;
        } catch (...) {}

        crow::mustache::context ctx;
        crow::json::wvalue arr;
        for (size_t i = 0; i < levels.size(); ++i) {
            arr[i]["id"]                          = levels[i].id;
            arr[i]["name"]                        = levels[i].name;
            arr[i]["discord_role_id"]              = levels[i].discord_role_id;
            auto rit = role_names.find(levels[i].discord_role_id);
            arr[i]["discord_role_name"]            = rit != role_names.end() ? ("@" + rit->second) :
                                                     (levels[i].discord_role_id.empty() ? "None" : levels[i].discord_role_id);
            arr[i]["meeting_attendance_required"]  = levels[i].meeting_attendance_required;
            arr[i]["event_attendance_required"]    = levels[i].event_attendance_required;
            arr[i]["requires_paid_dues"]           = levels[i].requires_paid_dues;
            arr[i]["min_fol_status"]               = levels[i].min_fol_status;
            arr[i]["sort_order"]                   = levels[i].sort_order;
        }
        ctx["perk_levels"]    = std::move(arr);
        ctx["has_perks"]      = !levels.empty();
        ctx["is_admin"]       = true;
        ctx["title"]          = "Perk Levels";
        ctx["selected_year"]  = selected_year;

        crow::json::wvalue year_arr;
        for (size_t i = 0; i < perk_years.size(); ++i) {
            year_arr[i]["year"]     = perk_years[i];
            year_arr[i]["selected"] = (perk_years[i] == selected_year);
        }
        ctx["years"] = std::move(year_arr);

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        auto tmpl = crow::mustache::load("settings/_perks.html");
        std::string content = tmpl.render(ctx).dump();
        if (is_htmx) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(content);
            return res;
        }

        crow::mustache::context layout_ctx;
        layout_ctx["content"]         = content;
        layout_ctx["page_title"]      = "Perk Levels";
        layout_ctx["active_settings"] = true;
        layout_ctx["is_admin"]        = true;
        set_layout_auth(req, app, layout_ctx);
        auto layout = crow::mustache::load("layout.html");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(layout.render(layout_ctx).dump());
        return res;
    });

    // POST /settings/perks — create perk level
    CROW_ROUTE(app, "/settings/perks").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };

        PerkLevel p;
        p.name = gp("name");
        if (p.name.empty()) {
            res.code = 400;
            res.write(R"(<div class="text-red-500 text-sm p-2">Name is required.</div>)");
            return res;
        }
        p.description = gp("description");
        p.discord_role_id = gp("discord_role_id");
        try { p.meeting_attendance_required = std::stoi(gp("meeting_attendance_required")); } catch (...) {}
        try { p.event_attendance_required = std::stoi(gp("event_attendance_required")); } catch (...) {}
        p.requires_paid_dues = (gp("requires_paid_dues") == "on" || gp("requires_paid_dues") == "1");
        { std::string fol = gp("min_fol_status"); p.min_fol_status = fol.empty() ? "afol" : fol; }
        try { p.sort_order = std::stoi(gp("sort_order")); } catch (...) {}
        try { p.year = std::stoi(gp("year")); } catch (...) {
            std::time_t now_t = std::time(nullptr);
            p.year = std::localtime(&now_t)->tm_year + 1900;
        }

        perks.create(p);
        audit.log(req, app, "perk.create", "perk", 0, p.name, "Created perk level");
        res.add_header("HX-Redirect", "/settings/perks?year=" + std::to_string(p.year));
        res.code = 200;
        return res;
    });

    // GET /settings/perks/<id>/edit — edit form in modal
    CROW_ROUTE(app, "/settings/perks/<int>/edit")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto p = perks.find_by_id(static_cast<int64_t>(id));
        if (!p) { res.code = 404; res.write("Not found"); return res; }

        std::string cls_input = "mt-1 block w-full border border-gray-300 rounded-lg px-3 py-2 text-sm";
        std::string cls_select = "mt-1 w-full border border-gray-300 rounded-lg px-3 py-2 text-sm";
        std::string checked_paid = p->requires_paid_dues ? " checked" : "";
        std::string sel_kfol = p->min_fol_status == "kfol" ? " selected" : "";
        std::string sel_tfol = p->min_fol_status == "tfol" ? " selected" : "";
        std::string sel_afol = p->min_fol_status == "afol" ? " selected" : "";

        std::ostringstream h;
        h << "<div class=\"p-6\">"
          << "<div class=\"flex items-center justify-between mb-5\">"
          << "<h3 class=\"text-lg font-semibold text-gray-800\">Edit Perk Level</h3>"
          << "<button onclick=\"closeModal()\" class=\"text-gray-400 hover:text-gray-600 p-1 rounded\">"
          << "<svg class=\"w-5 h-5\" fill=\"none\" stroke=\"currentColor\" viewBox=\"0 0 24 24\"><path stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"2\" d=\"M6 18L18 6M6 6l12 12\"/></svg></button></div>"
          << "<form hx-put=\"/settings/perks/" << id << "\" hx-swap=\"none\" class=\"space-y-4\">"
          << "<div class=\"grid grid-cols-2 gap-4\">"
          << "<div><label class=\"block text-sm font-medium text-gray-700\">Name</label>"
          << "<input type=\"text\" name=\"name\" value=\"" << p->name << "\" required class=\"" << cls_input << "\"></div>"
          << "<div><label class=\"block text-sm font-medium text-gray-700\">Sort Order</label>"
          << "<input type=\"number\" name=\"sort_order\" value=\"" << p->sort_order << "\" class=\"" << cls_input << "\"></div></div>"
          << "<div><label class=\"block text-sm font-medium text-gray-700\">Description</label>"
          << "<textarea name=\"description\" rows=\"2\" class=\"" << cls_input << "\" placeholder=\"What members get at this tier\">" << p->description << "</textarea></div>"
          << "<div class=\"grid grid-cols-2 gap-4\">"
          << "<div><label class=\"block text-sm font-medium text-gray-700\">Meetings Required</label>"
          << "<input type=\"number\" name=\"meeting_attendance_required\" value=\"" << p->meeting_attendance_required << "\" min=\"0\" class=\"" << cls_input << "\"></div>"
          << "<div><label class=\"block text-sm font-medium text-gray-700\">Events Required</label>"
          << "<input type=\"number\" name=\"event_attendance_required\" value=\"" << p->event_attendance_required << "\" min=\"0\" class=\"" << cls_input << "\"></div></div>"
          << "<div class=\"grid grid-cols-2 gap-4\">"
          << "<div><label class=\"block text-sm font-medium text-gray-700\">Discord Role</label>"
          << "<select name=\"discord_role_id\" hx-get=\"/api/discord/role-options?selected=" << p->discord_role_id << "\" hx-trigger=\"load\" hx-swap=\"innerHTML\""
          << " hx-on::after-settle=\"if(window.TomSelect&amp;&amp;!this.tomselect)new TomSelect(this,{maxOptions:null})\""
          << " class=\"" << cls_select << "\"><option value=\"\">Loading...</option></select></div>"
          << "<div class=\"flex flex-col justify-end gap-2 pb-1\">"
          << "<label class=\"flex items-center gap-2 text-sm text-gray-700\">"
          << "<input type=\"checkbox\" name=\"requires_paid_dues\" class=\"rounded border-gray-300\"" << checked_paid << ">"
          << "Requires Paid Dues</label></div></div>"
          << "<div><label class=\"block text-sm font-medium text-gray-700\">Minimum FOL Status</label>"
          << "<select name=\"min_fol_status\" class=\"" << cls_select << "\">"
          << "<option value=\"kfol\"" << sel_kfol << ">KFOL (Kid)</option>"
          << "<option value=\"tfol\"" << sel_tfol << ">TFOL (Teen)</option>"
          << "<option value=\"afol\"" << sel_afol << ">AFOL (Adult)</option>"
          << "</select></div>"
          << "<div class=\"flex gap-3 pt-2\">"
          << "<button type=\"submit\" class=\"flex-1 py-2 bg-gray-800 text-white rounded-lg text-sm font-medium hover:bg-gray-700\">Save Changes</button>"
          << "<button type=\"button\" onclick=\"closeModal()\" class=\"px-4 py-2 text-gray-600 border border-gray-300 rounded-lg text-sm hover:bg-gray-50\">Cancel</button>"
          << "</div></form></div>";

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(h.str());
        return res;
    });

    // PUT /settings/perks/<id> — update perk level
    CROW_ROUTE(app, "/settings/perks/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };

        auto existing = perks.find_by_id(static_cast<int64_t>(id));
        if (!existing) { res.code = 404; res.write("Not found"); return res; }

        PerkLevel p = *existing;
        std::string name = gp("name");
        if (!name.empty()) p.name = name;
        p.description = gp("description");
        p.discord_role_id = gp("discord_role_id");
        try { p.meeting_attendance_required = std::stoi(gp("meeting_attendance_required")); } catch (...) {}
        try { p.event_attendance_required = std::stoi(gp("event_attendance_required")); } catch (...) {}
        p.requires_paid_dues = (gp("requires_paid_dues") == "on" || gp("requires_paid_dues") == "1");
        { std::string fol = gp("min_fol_status"); p.min_fol_status = fol.empty() ? "afol" : fol; }
        try { p.sort_order = std::stoi(gp("sort_order")); } catch (...) {}

        perks.update(p);
        audit.log(req, app, "perk.update", "perk", p.id, p.name, "Updated perk level");
        res.add_header("HX-Redirect", "/settings/perks");
        res.code = 200;
        return res;
    });

    // DELETE /settings/perks/<id>
    CROW_ROUTE(app, "/settings/perks/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        perks.remove(static_cast<int64_t>(id));
        audit.log(req, app, "perk.delete", "perk", static_cast<int64_t>(id), "", "Deleted perk level");
        res.add_header("HX-Redirect", "/settings/perks");
        res.code = 200;
        return res;
    });

    // POST /settings/perks/clone — clone tiers from one year to another
    CROW_ROUTE(app, "/settings/perks/clone").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string { const char* v = params.get(k); return v ? v : ""; };

        int source_year = 0, target_year = 0;
        try { source_year = std::stoi(gp("source_year")); } catch (...) {}
        try { target_year = std::stoi(gp("target_year")); } catch (...) {}

        if (source_year == 0 || target_year == 0 || source_year == target_year) {
            res.code = 400;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-red-500 text-xs">Invalid source or target year.</span>)");
            return res;
        }

        // Check target year doesn't already have tiers
        auto existing = perks.find_by_year(target_year);
        if (!existing.empty()) {
            res.code = 400;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<span class=\"text-red-500 text-xs\">" + std::to_string(target_year) +
                      " already has " + std::to_string(existing.size()) + " tiers. Delete them first.</span>");
            return res;
        }

        perks.clone_year(source_year, target_year);
        audit.log(req, app, "perk.clone", "perk", 0, "", "Cloned from year " + std::to_string(source_year) + " to year " + std::to_string(target_year));
        res.add_header("HX-Redirect", "/settings/perks?year=" + std::to_string(target_year));
        res.code = 200;
        return res;
    });

    // POST /api/perks/sync-roles — bulk sync perk Discord roles for all members
    CROW_ROUTE(app, "/api/perks/sync-roles").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        // Sync uses current year's tiers
        std::time_t now_sync = std::time(nullptr);
        int sync_year = std::localtime(&now_sync)->tm_year + 1900;
        auto levels = perks.find_by_year(sync_year);
        if (levels.empty()) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<span class=\"text-yellow-600 text-xs\">No perk levels defined for " +
                      std::to_string(sync_year) + ".</span>");
            return res;
        }

        // Get current year
        std::time_t now = std::time(nullptr);
        std::tm* tm = std::localtime(&now);
        int year = tm->tm_year + 1900;

        auto all_members = members.find_all();
        int synced = 0;
        for (const auto& m : all_members) {
            int meeting_count = attendance.count_member_by_year(m.id, year, "meeting");
            int event_count   = attendance.count_member_by_year(m.id, year, "event");

            auto achieved = compute_perk_level(levels, meeting_count, event_count, m.is_paid, m.fol_status);

            for (const auto& lvl : levels) {
                if (lvl.discord_role_id.empty() || m.discord_user_id.empty()) continue;

                // Compute if member meets THIS specific tier
                bool meets_tier = meeting_count >= lvl.meeting_attendance_required &&
                                  event_count >= lvl.event_attendance_required &&
                                  (!lvl.requires_paid_dues || m.is_paid) &&
                                  fol_rank(m.fol_status) >= fol_rank(lvl.min_fol_status);

                if (meets_tier) {
                    try { discord.add_member_role(m.discord_user_id, lvl.discord_role_id); }
                    catch (...) {}
                } else {
                    try { discord.remove_member_role(m.discord_user_id, lvl.discord_role_id); }
                    catch (...) {}
                }
            }
            ++synced;
        }

        audit.log(req, app, "perk.sync_roles", "perk", 0, "", "Synced " + std::to_string(synced) + " members");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write("<span class=\"text-green-600 text-xs\">Synced " +
                  std::to_string(synced) + " members.</span>");
        return res;
    });
}
