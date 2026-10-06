#include "routes/members/MemberRoutes.hpp"
#include "routes/accounts/AccountSecurityRoutes.hpp"
#include "auth/AccountSecurity.hpp"
#include "utils/AuditDiff.hpp"
#include "utils/text/JsonEscape.hpp"
#include "utils/text/HtmlEscape.hpp"
#include "utils/web/ParseId.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <sstream>
#include <unordered_set>

// Allowed sort column names (validated against this set)
static const std::unordered_set<std::string> kAllowedSortCols = {
    "display_name", "discord_username", "email", "is_paid", "role", "chapter_name", "fol_status"
};

static crow::mustache::context member_to_ctx(const Member& m) {
    crow::mustache::context ctx;
    ctx["id"]               = m.id;
    ctx["discord_user_id"]  = m.discord_user_id;
    ctx["discord_username"] = m.discord_username;
    ctx["has_discord_username"] = !m.discord_username.empty();
    ctx["first_name"]       = m.first_name;
    ctx["last_name"]        = m.last_name;
    ctx["display_name"]     = m.display_name;
    ctx["email"]            = m.email;
    ctx["is_paid"]          = m.is_paid;
    ctx["paid_until"]       = m.paid_until;
    ctx["role"]             = m.role;
    ctx["role_admin"]        = m.role == "admin";
    ctx["role_chapter_lead"] = m.role == "chapter_lead";
    ctx["show_chapter_lead"] = Features::on("chapters") || m.role == "chapter_lead";
    ctx["chapters_off"] = !Features::on("chapters");
    ctx["role_moderator"]    = m.role == "moderator";
    ctx["role_member"]       = m.role == "member" || m.role.empty();
    ctx["birthday"]          = m.birthday;
    ctx["fol_status"]        = m.fol_status;
    ctx["fol_kfol"]          = m.fol_status == "kfol";
    ctx["fol_tfol"]          = m.fol_status == "tfol";
    ctx["fol_afol"]          = m.fol_status == "afol" || m.fol_status.empty();
    auto sharing_ctx = [&](const std::string& prefix, const std::string& val) {
        ctx[prefix]               = val;
        ctx[prefix + "_none"]     = (val == "none" || val.empty());
        ctx[prefix + "_verified"] = (val == "verified");
        ctx[prefix + "_all"]      = (val == "all");
    };
    sharing_ctx("sharing_email",    m.sharing_email);
    sharing_ctx("sharing_phone",    m.sharing_phone);
    sharing_ctx("sharing_address",  m.sharing_address);
    sharing_ctx("sharing_birthday", m.sharing_birthday);
    sharing_ctx("sharing_discord",  m.sharing_discord);
    ctx["phone"]             = m.phone;
    ctx["address_line1"]     = m.address_line1;
    ctx["address_line2"]     = m.address_line2;
    ctx["city"]              = m.city;
    ctx["state"]             = m.state;
    ctx["zip"]               = m.zip;
    ctx["chapter_id"]        = m.chapter_id;
    ctx["chapter_id_str"]    = m.chapter_id > 0 ? std::to_string(m.chapter_id) : "";
    ctx["chapter_name"]      = m.chapter_name;
    ctx["created_at"]        = m.created_at;
    return ctx;
}

static std::string render_members_page(const crow::request& req,
                                        LugApp& app) {
    auto& auth = app.get_context<AuthMiddleware>(req).auth;
    bool is_admin = auth.is_admin();
    bool can_see_pii = auth.can("members.view_private");
    bool can_see_dues = auth.can("members.view_private");
    crow::mustache::context ctx;
    ctx["title"]        = "Members";
    ctx["is_admin"]     = is_admin;
    ctx["can_see_pii"]  = can_see_pii;
    ctx["can_see_dues"] = can_see_dues;
    ctx["can_edit_members"] = auth.can("members.edit");
    ctx["can_export"]       = auth.can("members.export");
    ctx["can_record_dues"]  = auth.can("dues.record");
    ctx["can_open_dues"]    = can_see_dues || auth.can("dues.record");
    Features::add_flags(ctx);

    const bool htmx = is_htmx(req);
    if (htmx) {
        auto tmpl = crow::mustache::load("members/_content.html");
        return tmpl.render(ctx).dump();
    }

    auto content_tmpl = crow::mustache::load("members/_content.html");
    std::string content = content_tmpl.render(ctx).dump();
    return render_in_layout(req, app, content, "Members", "active_members");
}

// Guardian and consent are for KFOL/TFOL members only, and the guardian is
// another adult member of the club (picked from the members list).
static bool is_minor(const std::string& fol) { return fol == "kfol" || fol == "tfol"; }

// Reads the guardian/consent block of the member form (only when it was on the form).
template <typename GetParam>
static std::optional<MemberRepository::Guardian> guardian_from_form(GetParam get_param) {
    if (get_param("guardian_form") != "1") return std::nullopt;
    MemberRepository::Guardian g;
    g.member_id       = parse_id(get_param("guardian_member_id"));
    g.consent_on_file = get_param("consent_on_file") == "1";
    g.consent_date    = get_param("consent_date").substr(0, 10);
    g.photo_release   = get_param("photo_release") == "1";
    return g;
}

// "" when `guardian_id` may be `self_id`'s guardian, else why not.
static std::string guardian_problem(MemberRepository& repo, int64_t self_id, int64_t guardian_id) {
    if (guardian_id <= 0) return "";
    if (guardian_id == self_id) return "A member can't be their own guardian.";
    auto st = repo.db().prepare("SELECT COALESCE(fol_status,'afol') FROM members WHERE id=?");
    st.bind(1, guardian_id);
    if (!st.step()) return "Pick the guardian from the members list.";
    if (is_minor(st.col_text(0))) return "The guardian has to be an adult member, not a KFOL or TFOL.";
    return "";
}

// Saves the guardian block: cleared for adults; picking a member replaces the
// details typed in before guardians were members; otherwise those stay.
static void save_guardian(MemberRepository& repo, int64_t id, const std::string& fol, MemberRepository::Guardian g) {
    if (!is_minor(fol)) {
        g = MemberRepository::Guardian{};
    } else if (g.member_id <= 0) {
        auto old = repo.get_guardian(id);
        g.name = old.name; g.phone = old.phone; g.email = old.email;
    }
    repo.set_guardian(id, g);
}

// The member form's guardian picker: every adult member but themselves.
static crow::json::wvalue guardian_options(MemberRepository& repo, int64_t self_id, int64_t selected) {
    crow::json::wvalue arr = crow::json::wvalue::list();
    auto st = repo.db().prepare("SELECT id, display_name FROM members WHERE id<>? AND COALESCE(fol_status,'afol') "
                                "NOT IN ('kfol','tfol') ORDER BY display_name COLLATE NOCASE");
    st.bind(1, self_id);
    int i = 0;
    while (st.step()) {
        arr[i]["id"] = st.col_int(0); arr[i]["name"] = st.col_text(1);
        arr[i]["selected"] = st.col_int(0) == selected;
        ++i;
    }
    return arr;
}

static std::string error_box(const std::string& msg) {
    return R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">)" + html_escape(msg) + "</div>";
}

void register_member_routes(LugApp& app, MemberService& members, AttendanceRepository& attendance_repo, AuditService& audit,
                            std::shared_ptr<Notifier> notifier) {
    MemberRepository& member_repo = members.repo();

    // GET /members - members page (table shell; data loaded via AJAX)
    CROW_ROUTE(app, "/members")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_members_page(req, app));
        return res;
    });

    // POST /api/members/datatable - DataTables server-side AJAX endpoint
    CROW_ROUTE(app, "/api/members/datatable").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto params = crow::query_string("?" + req.body);
        auto get_p  = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        DatatableParams p;
        try { p.draw   = std::stoi(get_p("draw")); }   catch (...) {}
        try { p.start  = std::stoi(get_p("start")); }  catch (...) {}
        try { p.length = std::stoi(get_p("length")); } catch (...) {}
        if (p.length < 1)  p.length = 25;
        if (p.length > 200) p.length = 200;

        p.search = get_p("search");

        std::string col_name = get_p("order_col_name");
        if (!col_name.empty() && kAllowedSortCols.count(col_name))
            p.sort_col = col_name;
        p.sort_dir = get_p("order_dir");

        auto result = members.datatable(p);

        // Access checks: admin/chapter_lead see PII + dues; regular members see
        // limited info — PII only for opted-in members, no dues, no discord username
        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        bool role_can_see_pii = auth.can("members.view_private");
        bool can_see_dues = auth.can("members.view_private");
        bool viewer_is_verified = role_can_see_pii || attendance_repo.is_verified_member(auth.member_id);

        // Build JSON manually so "data" is always a proper [] array.
        // Crow's default wvalue serialises as null when no indices are set.
        auto esc_json = [](const std::string& s) -> std::string {
            std::string r; r.reserve(s.size());
            for (unsigned char c : s) {
                if      (c == '"')  r += "\\\"";
                else if (c == '\\') r += "\\\\";
                else if (c == '\n') r += "\\n";
                else if (c == '\r') r += "\\r";
                else if (c == '\t') r += "\\t";
                else if (c < 0x20) { char buf[7]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); r += buf; }
                else r += static_cast<char>(c);
            }
            return r;
        };

        std::ostringstream json;
        json << "{\"draw\":"            << result.draw
             << ",\"recordsTotal\":"    << result.records_total
             << ",\"recordsFiltered\":" << result.records_filtered
             << ",\"data\":[";
        for (size_t i = 0; i < result.data.size(); ++i) {
            if (i > 0) json << ",";
            const auto& m = result.data[i];
            auto can_see = [&](const std::string& sharing) {
                return role_can_see_pii || sharing == "all"
                    || (sharing == "verified" && viewer_is_verified);
            };
            json << "{\"DT_RowId\":\"member-row-" << m.id << "\""
                 << ",\"id\":"               << m.id
                 << ",\"display_name\":\""   << esc_json(m.display_name) << "\""
                 << ",\"first_name\":\""     << esc_json(m.first_name) << "\""
                 << ",\"last_name\":\""      << esc_json(m.last_name) << "\""
                 << ",\"discord_username\":\"" << (can_see(m.sharing_discord) ? esc_json(m.discord_username) : "") << "\""
                 << ",\"email\":\""          << (can_see(m.sharing_email) ? esc_json(m.email) : "") << "\""
                 << ",\"is_paid\":"          << (can_see_dues ? (m.is_paid ? "true" : "false") : "null")
                 << ",\"paid_until\":\""     << (can_see_dues ? esc_json(m.paid_until) : "") << "\""
                 << ",\"role\":\""           << esc_json(m.role) << "\""
                 << ",\"chapter_name\":\""   << esc_json(m.chapter_name) << "\""
                 << ",\"fol_status\":\""    << esc_json(m.fol_status) << "\""
                 << "}";
        }
        json << "]}";

        res.add_header("Content-Type", "application/json");
        res.write(json.str());
        return res;
    });

    // GET /members/me - self-edit profile form (any authenticated member)
    CROW_ROUTE(app, "/members/me")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        auto m = members.get(auth.member_id);
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Member not found.</div>");
            return res;
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("members/_self_edit.html");
        auto ctx  = member_to_ctx(*m);
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // POST /members/me - update own PII (any authenticated member)
    CROW_ROUTE(app, "/members/me").methods("POST"_method)(
        [&, notifier](const crow::request& req) {   // by value: it's this function's parameter
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        auto existing = members.get(auth.member_id);
        if (!existing) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Member not found.</div>");
            return res;
        }

        auto params    = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        // Only allow PII fields — preserve all admin-managed fields
        Member updates = *existing;
        updates.first_name     = get_param("first_name");
        updates.last_name      = get_param("last_name");
        updates.email          = get_param("email");
        updates.phone          = get_param("phone");
        updates.address_line1  = get_param("address_line1");
        updates.address_line2  = get_param("address_line2");
        updates.city           = get_param("city");
        updates.state          = get_param("state");
        updates.zip            = get_param("zip");
        updates.birthday       = get_param("birthday");
        {
            auto parse_sharing = [&](const char* name) -> std::string {
                std::string v = get_param(name);
                return (v == "verified" || v == "all") ? v : "none";
            };
            updates.sharing_email    = parse_sharing("sharing_email");
            updates.sharing_phone    = parse_sharing("sharing_phone");
            updates.sharing_address  = parse_sharing("sharing_address");
            updates.sharing_birthday = parse_sharing("sharing_birthday");
            updates.sharing_discord  = parse_sharing("sharing_discord");
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto before_self = members.get(auth.member_id);
        try {
            auto after_self = members.update(auth.member_id, updates);
            // Their own new email: ask them to confirm it's theirs.
            if (before_self && !after_self.email.empty() &&
                AccountSecurity::lower(before_self->email) != AccountSecurity::lower(after_self.email))
                request_email_confirmation(members.repo().db(), notifier.get(), auth.member_id);
            {
                AuditDiff diff;
                if (before_self) {
                    diff.field("first_name", before_self->first_name, after_self.first_name);
                    diff.field("last_name", before_self->last_name, after_self.last_name);
                    diff.field("display_name", before_self->display_name, after_self.display_name);
                    diff.field("email", before_self->email, after_self.email);
                    diff.field("phone", before_self->phone, after_self.phone);
                    diff.field("address_line1", before_self->address_line1, after_self.address_line1);
                    diff.field("address_line2", before_self->address_line2, after_self.address_line2);
                    diff.field("city", before_self->city, after_self.city);
                    diff.field("state", before_self->state, after_self.state);
                    diff.field("zip", before_self->zip, after_self.zip);
                    diff.field("birthday", before_self->birthday, after_self.birthday);
                    diff.field("sharing_email", before_self->sharing_email, after_self.sharing_email);
                    diff.field("sharing_phone", before_self->sharing_phone, after_self.sharing_phone);
                    diff.field("sharing_address", before_self->sharing_address, after_self.sharing_address);
                    diff.field("sharing_birthday", before_self->sharing_birthday, after_self.sharing_birthday);
                    diff.field("sharing_discord", before_self->sharing_discord, after_self.sharing_discord);
                }
                audit.log(req, app, "member.self_edit", "member", auth.member_id, auth.display_name,
                          diff.has_changes() ? diff.str() : "No field changes");
            }
            res.add_header("HX-Trigger", "{\"closeModal\":true,\"membersUpdated\":true}");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(
                R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">)"
                "Error: ") + html_escape(e.what()) + "</div>");
        }
        return res;
    });

    // GET /members/<id>/view - read-only member detail modal (any authenticated user)
    CROW_ROUTE(app, "/members/<int>/view")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto m = members.get(static_cast<int64_t>(id));
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Member not found.</div>");
            return res;
        }

        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        bool privileged = auth.can("members.view_private");
        bool viewer_verified = privileged || attendance_repo.is_verified_member(auth.member_id);
        bool is_self = (auth.member_id == m->id);

        auto can_see = [&](const std::string& sharing) {
            return is_self || privileged || sharing == "all"
                || (sharing == "verified" && viewer_verified);
        };

        auto ctx = member_to_ctx(*m);
        ctx["show_email"]    = can_see(m->sharing_email);
        ctx["show_phone"]    = can_see(m->sharing_phone);
        ctx["show_address"]  = can_see(m->sharing_address);
        ctx["show_birthday"] = can_see(m->sharing_birthday);
        ctx["show_discord"]  = can_see(m->sharing_discord);
        ctx["show_any_pii"]  = can_see(m->sharing_email) || can_see(m->sharing_phone)
                             || can_see(m->sharing_address) || can_see(m->sharing_birthday)
                             || can_see(m->sharing_discord);
        ctx["show_dues"]     = (privileged || is_self) && Features::on("dues");
        if (!Features::on("chapters")) ctx["chapter_id_str"] = "";
        ctx["viewer_is_admin"] = auth.is_admin() && !is_self;
        if (privileged || is_self) {
            auto g = member_repo.get_guardian(m->id);
            ctx["show_guardian"]   = is_minor(m->fol_status) && Features::on("guardians");
            if (g.member_id > 0) {
                ctx["guardian_member_id"] = g.member_id;
                ctx["guardian_name"]  = g.member_name;
                ctx["guardian_phone"] = g.member_phone;
                ctx["guardian_email"] = g.member_email;
            } else {
                ctx["guardian_name"]  = g.name;      // typed in before guardians were members
                ctx["guardian_phone"] = g.phone;
                ctx["guardian_email"] = g.email;
                ctx["guardian_unlinked"] = !g.name.empty();
            }
            ctx["consent_on_file"] = g.consent_on_file;
            ctx["consent_date"]    = g.consent_date;
            ctx["photo_release"]   = g.photo_release;
        }
        ctx["is_self"]       = is_self;

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("members/_view.html");
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // GET /members/new - new member form fragment (chapter_lead+)
    CROW_ROUTE(app, "/members/new")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:members.edit")) return res;

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("members/_form.html");
        crow::mustache::context ctx;
        ctx["action"] = "/members";
        ctx["title"]  = "Add Member";
        ctx["is_new"] = true;
        ctx["guardian_options"] = guardian_options(member_repo, 0, 0);
        ctx["show_chapter_lead"] = Features::on("chapters");
        ctx["is_admin"] = app.get_context<AuthMiddleware>(req).auth.is_admin();
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // GET /members/<id> - member edit form fragment (chapter_lead+ - contains PII)
    CROW_ROUTE(app, "/members/<int>")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:members.edit")) return res;

        auto m = members.get(static_cast<int64_t>(id));
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Member not found.</div>");
            return res;
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("members/_form.html");
        auto ctx  = member_to_ctx(*m);
        {
            auto g = member_repo.get_guardian(m->id);
            ctx["guardian_options"] = guardian_options(member_repo, m->id, g.member_id);
            if (g.member_id <= 0 && !g.name.empty()) {
                ctx["guardian_legacy"] = g.name + (g.phone.empty() ? "" : ", " + g.phone) + (g.email.empty() ? "" : ", " + g.email);
            }
            ctx["consent_on_file"] = g.consent_on_file; ctx["consent_date"] = g.consent_date;
            ctx["photo_release"] = g.photo_release;
        }
        ctx["action"]   = "/members/" + std::to_string(id);
        ctx["title"]    = "Edit Member";
        ctx["is_edit"]  = true;
        ctx["is_admin"] = app.get_context<AuthMiddleware>(req).auth.is_admin();
        {
            auto t = member_repo.db().prepare("SELECT is_treasurer FROM members WHERE id=?");
            t.bind(1, m->id);
            ctx["is_treasurer"] = t.step() && t.col_int(0) != 0;
        }
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // POST /members - create member (form POST, chapter_lead+)
    CROW_ROUTE(app, "/members").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:members.edit")) return res;

        auto params    = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        bool caller_is_admin = app.get_context<AuthMiddleware>(req).auth.is_admin();

        Member m;
        m.discord_user_id  = get_param("discord_user_id");
        m.discord_username = get_param("discord_username");
        m.first_name       = get_param("first_name");
        m.last_name        = get_param("last_name");
        m.email            = get_param("email");
        std::string req_role = get_param("role");
        // Only admins give out roles: anyone else adds plain members (a new
        // record with someone's Discord ID would otherwise hand them a role).
        if (!caller_is_admin) req_role = "member";
        m.role             = Features::normalize_role(req_role.empty() ? "member" : req_role);
        m.birthday         = get_param("birthday");
        m.fol_status       = get_param("fol_status").empty() ? "afol" : get_param("fol_status");
        m.phone            = get_param("phone");
        m.address_line1    = get_param("address_line1");
        m.address_line2    = get_param("address_line2");
        m.city             = get_param("city");
        m.state            = get_param("state");
        m.zip              = get_param("zip");
        // PII sharing defaults to "none" — only the member can change via self-edit

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto guardian = guardian_from_form(get_param);
        if (guardian && is_minor(m.fol_status)) {
            std::string why = guardian_problem(member_repo, 0, guardian->member_id);
            if (!why.empty()) { res.code = 400; res.write(error_box(why)); return res; }
        }
        try {
            auto created = members.create(m);
            if (guardian) save_guardian(member_repo, created.id, m.fol_status, *guardian);
            audit.log(req, app, "member.create", "member", created.id, created.display_name,
                      "Created member: " + created.first_name + " " + created.last_name);
            res.add_header("HX-Trigger", "{\"closeModal\":true,\"membersUpdated\":true}");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(
                R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">)"
                "Error: ") + html_escape(e.what()) + "</div>");
        }
        return res;
    });

    // POST /members/<id> - update member (form body, from edit modal, chapter_lead+)
    CROW_ROUTE(app, "/members/<int>").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:members.edit")) return res;

        bool caller_is_admin = app.get_context<AuthMiddleware>(req).auth.is_admin();

        auto params    = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        Member updates;
        updates.first_name       = get_param("first_name");
        updates.last_name        = get_param("last_name");
        updates.discord_username = get_param("discord_username");
        updates.email            = get_param("email");
        // The email is a sign-in address (password reset, email links): if a
        // chapter lead could change it, they could take over the account -
        // an admin's included. Only admins (or the member, on their own page).
        if (!caller_is_admin) {
            auto cur = members.get(static_cast<int64_t>(id));
            updates.email = cur ? cur->email : "";
        }
        // Only admins may change an existing member's LUG role - otherwise a
        // chapter lead could demote an admin or promote a peer. Empty = unchanged.
        updates.role             = caller_is_admin ? get_param("role") : "";
        if (!updates.role.empty()) {
            auto cur = members.get(static_cast<int64_t>(id));
            updates.role = Features::normalize_role(updates.role, cur ? cur->role : "");
        }
        updates.birthday         = get_param("birthday");
        updates.fol_status       = get_param("fol_status").empty() ? "afol" : get_param("fol_status");
        updates.phone            = get_param("phone");
        updates.address_line1    = get_param("address_line1");
        updates.address_line2    = get_param("address_line2");
        updates.city             = get_param("city");
        updates.state            = get_param("state");
        updates.zip              = get_param("zip");
        // PII sharing settings are NOT editable by admins — only the member can change their own

        std::string paid_until = get_param("paid_until");
        updates.paid_until = paid_until;
        updates.is_paid    = !paid_until.empty();

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto guardian = guardian_from_form(get_param);
        if (guardian && is_minor(updates.fol_status)) {
            std::string why = guardian_problem(member_repo, id, guardian->member_id);
            if (!why.empty()) { res.code = 400; res.write(error_box(why)); return res; }
        }
        auto before = members.get(static_cast<int64_t>(id));
        try {
            members.update(static_cast<int64_t>(id), updates);
            std::string chapter_str = get_param("chapter_id");
            int64_t new_chapter_id = parse_id(chapter_str);
            members.set_chapter(static_cast<int64_t>(id), new_chapter_id);
            if (guardian) save_guardian(member_repo, id, updates.fol_status, *guardian);
            if (caller_is_admin && get_param("treasurer_form") == "1") {
                bool want = get_param("is_treasurer") == "1";
                auto t = member_repo.db().prepare("UPDATE members SET is_treasurer=? WHERE id=? AND is_treasurer<>? RETURNING display_name");
                t.bind(1, static_cast<int64_t>(want)); t.bind(2, static_cast<int64_t>(id)); t.bind(3, static_cast<int64_t>(want));
                if (t.step()) {
                    std::string who = t.col_text(0);
                    t.reset();
                    audit.log(req, app, "member.treasurer", "member", id, who, want ? "Made treasurer" : "No longer treasurer");
                }
            }
            // Re-read after all changes (including chapter) for accurate diff
            auto after = members.get(static_cast<int64_t>(id));
            {
                AuditDiff diff;
                if (before && after) {
                    diff.field("first_name", before->first_name, after->first_name);
                    diff.field("last_name", before->last_name, after->last_name);
                    diff.field("display_name", before->display_name, after->display_name);
                    diff.field("discord_username", before->discord_username, after->discord_username);
                    diff.field("email", before->email, after->email);
                    diff.field("role", before->role, after->role);
                    diff.field("phone", before->phone, after->phone);
                    diff.field("address_line1", before->address_line1, after->address_line1);
                    diff.field("address_line2", before->address_line2, after->address_line2);
                    diff.field("city", before->city, after->city);
                    diff.field("state", before->state, after->state);
                    diff.field("zip", before->zip, after->zip);
                    diff.field("birthday", before->birthday, after->birthday);
                    diff.field("is_paid", before->is_paid, after->is_paid);
                    diff.field("paid_until", before->paid_until, after->paid_until);
                    diff.field("fol_status", before->fol_status, after->fol_status);
                    {
                        // Resolve chapter names for the diff
                        std::string old_ch = before->chapter_id > 0 ? before->chapter_name : "none";
                        std::string new_ch = after->chapter_id > 0 ? after->chapter_name : "none";
                        if (old_ch.empty() && before->chapter_id > 0) old_ch = "chapter #" + std::to_string(before->chapter_id);
                        if (new_ch.empty() && after->chapter_id > 0) new_ch = "chapter #" + std::to_string(after->chapter_id);
                        diff.field("chapter", old_ch, new_ch);
                    }
                }
                std::string name = after ? after->display_name : (before ? before->display_name : "");
                audit.log(req, app, "member.update", "member", id, name,
                          diff.has_changes() ? diff.str() : "No field changes");
            }
            res.add_header("HX-Trigger", "{\"closeModal\":true,\"membersUpdated\":true}");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(
                R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">)"
                "Error: ") + html_escape(e.what()) + "</div>");
        }
        return res;
    });

    // PUT /members/<id> - update member (JSON API, chapter_lead+)
    CROW_ROUTE(app, "/members/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:members.edit")) return res;

        auto body = crow::json::load(req.body);
        if (!body) {
            res.code = 400;
            res.write(R"({"error":"Invalid JSON"})");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        // Only admins may change LUG roles (see the form handler above);
        // without this a chapter lead could PUT {"role":"admin"} on themselves.
        // Same for the sign-in email (account takeover via password reset).
        if ((body.has("role") || body.has("email")) && !app.get_context<AuthMiddleware>(req).auth.is_admin()) {
            res.code = 403;
            res.write(R"({"error":"only admins can change roles and emails"})");
            res.add_header("Content-Type", "application/json");
            return res;
        }
        auto existing = members.get(static_cast<int64_t>(id));
        if (!existing) {
            res.code = 404;
            res.write(R"({"error":"member not found"})");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        Member updates = *existing;   // partial update: fields not sent stay as they are
        if (body.has("display_name")) updates.display_name = body["display_name"].s();
        if (body.has("email"))        updates.email        = body["email"].s();
        if (body.has("role"))         updates.role         = body["role"].s();
        if (body.has("is_paid"))      updates.is_paid      = body["is_paid"].b();
        if (body.has("paid_until"))   updates.paid_until   = body["paid_until"].s();

        try {
            auto updated = members.update(static_cast<int64_t>(id), updates);
            audit.log(req, app, "member.update", "member", static_cast<int64_t>(id), updated.display_name, "Updated member (JSON API)");
            crow::json::wvalue resp;
            resp["id"]           = updated.id;
            resp["display_name"] = updated.display_name;
            resp["success"]      = true;
            res.write(resp.dump());
            res.add_header("Content-Type", "application/json");
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + json_escape(e.what()) + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // DELETE /members/<id> - delete member (admin only)
    CROW_ROUTE(app, "/members/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        try {
            auto del_member = members.get(static_cast<int64_t>(id));
            std::string del_name = del_member ? del_member->display_name : "ID:" + std::to_string(id);
            members.delete_member(static_cast<int64_t>(id));
            audit.log(req, app, "member.delete", "member", id, del_name, "Deleted member");
            res.add_header("HX-Trigger", "membersUpdated");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + json_escape(e.what()) + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // POST /members/<id>/paid - set paid status (chapter_lead+)
    CROW_ROUTE(app, "/members/<int>/paid").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:dues.record")) return res;

        auto params        = crow::query_string("?" + req.body);
        const char* v      = params.get("paid_until");
        std::string paid_until = v ? std::string(v) : "";
        bool is_paid           = !paid_until.empty();

        try {
            members.set_paid(static_cast<int64_t>(id), is_paid, paid_until);
            {
                auto pm = members.get(static_cast<int64_t>(id));
                std::string pname = pm ? pm->display_name : "ID:" + std::to_string(id);
                audit.log(req, app, "member.dues", "member", id, pname,
                          is_paid ? "Marked paid until " + paid_until : "Marked unpaid");
            }
            res.add_header("HX-Trigger", "duesUpdated");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + json_escape(e.what()) + "\"}");
        }
        res.add_header("Content-Type", "application/json");
        return res;
    });
}
