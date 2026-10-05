#include "routes/attendance/CheckinRoutes.hpp"
#include "live/LiveHub.hpp"
#include "utils/web/AssetVersion.hpp"
#include "utils/text/HtmlEscape.hpp"
#include "routes/events/EventAccess.hpp"
#include <algorithm>
#include <crow.h>
#include <crow/mustache.h>
#include <sstream>
#include <chrono>
#include <ctime>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace {

// A check-in token resolved to the entity it currently admits check-ins for.
struct CheckinTarget {
    std::string entity_type; // "meeting" | "event"
    int64_t     entity_id = 0;
    std::string title, date, location;
};

// Single source of truth for "does this token admit check-ins right now?",
// shared by every public /checkin route. A token is only live:
//  - meetings: not virtual, not cancelled, and today is the meeting's date
//    (+/- 1 day, since meeting times are LUG-local and the server clock may
//    be UTC);
//  - events:   not cancelled, and today is one of the event's days.
// Without this a leaked/old QR code worked forever.
std::optional<CheckinTarget> resolve_checkin(const std::string& token,
                                             MeetingRepository& meeting_repo,
                                             EventRepository& event_repo,
                                             AttendanceService& attendance) {
    if (auto mtg = meeting_repo.find_by_checkin_token(token)) {
        if (mtg->is_virtual || !AttendanceService::meeting_self_checkin_open(*mtg)) return std::nullopt;
        return CheckinTarget{"meeting", mtg->id, mtg->title, mtg->start_time, mtg->location};
    }
    if (auto ev = event_repo.find_by_checkin_token(token)) {
        if (ev->status == "cancelled") return std::nullopt;
        if (!attendance.event_day_repo().find_by_event_and_date(ev->id, AttendanceService::today_ymd()))
            return std::nullopt;
        return CheckinTarget{"event", ev->id, ev->title, ev->start_time, ev->location};
    }
    return std::nullopt;
}

// Caps how many brand-new member records one check-in link can create per
// hour. /manual is unauthenticated, so without a cap anyone holding a live
// QR link could flood the member list. Generous enough for a big event
// walk-in rush; in-memory (resets on restart), which is fine for this.
class NewMemberLimiter {
public:
    static constexpr int kPerHour = 60;

    bool allow(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto& w = windows_[token];
        if (now - w.start > std::chrono::hours(1)) w = {now, 0};
        if (w.count >= kPerHour) return false;
        ++w.count;
        return true;
    }

private:
    struct Window { std::chrono::steady_clock::time_point start{}; int count = 0; };
    std::mutex mutex_;
    std::unordered_map<std::string, Window> windows_;
};

// "Tue, Sep 30 · 7:00 PM" from "2026-09-30T19:00:00" (LUG-local, shown as-is).
std::string friendly_date(const std::string& iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    int n = std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi);
    if (n < 3 || mo < 1 || mo > 12) return iso;
    std::tm t{};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = 12;
    timegm(&t);
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[64];
    if (n >= 5)
        std::snprintf(buf, sizeof(buf), "%s, %s %d \u00b7 %d:%02d %s", days[t.tm_wday], months[mo - 1], d,
                      h % 12 == 0 ? 12 : h % 12, mi, h >= 12 ? "PM" : "AM");
    else
        std::snprintf(buf, sizeof(buf), "%s, %s %d", days[t.tm_wday], months[mo - 1], d);
    return buf;
}

} // namespace

void register_checkin_routes(LugApp& app,
                              MeetingRepository& meeting_repo,
                              EventRepository& event_repo,
                              MeetingService& meetings,
                              EventService& events,
                              AttendanceService& attendance,
                              MemberService& members,
                              MemberRepository& member_repo,
                              ChapterMemberRepository& chapter_members,
                              DiscordOAuth& /*oauth*/,
                              AuditService& audit) {

    // POST /meetings/<id>/generate-checkin — generate or return existing QR check-in token
    CROW_ROUTE(app, "/meetings/<int>/generate-checkin").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto mtg = meetings.get(static_cast<int64_t>(id));
        if (!mtg) { res.code = 404; res.write("Not found"); return res; }

        // No QR check-in for virtual meetings
        if (mtg->is_virtual) {
            res.code = 400;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-gray-400 text-xs">QR check-in is not available for virtual meetings.</span>)");
            return res;
        }

        // Permission: admin, chapter lead/event_manager for this chapter
        if (!can_manage_chapter_content(req, res, app, mtg->chapter_id, chapter_members))
            return res;

        // Generate token if not exists
        std::string token = mtg->checkin_token;
        if (token.empty()) {
            token = MeetingService::generate_uuid();
            meeting_repo.update_checkin_token(mtg->id, token);
        }

        // Return the QR code display HTML
        std::ostringstream html;
        html << "<div class=\"text-center p-4\">"
             << "<div id=\"checkin-qr\" class=\"inline-block bg-white p-4 rounded-xl border border-gray-200\"></div>"
             << "<p class=\"text-xs text-gray-500 mt-3\">Scan to check in to this meeting</p>"
             << "<input type=\"text\" readonly value=\"" << token << "\" "
             << "class=\"mt-2 w-full text-center text-xs font-mono bg-gray-50 border border-gray-200 rounded px-2 py-1 select-all\">"
             << "</div>"
             << "<script>"
             << "if(typeof QRCode!=='undefined'){"
             << "document.getElementById('checkin-qr').innerHTML='';"
             << "new QRCode(document.getElementById('checkin-qr'),{text:window.location.origin+'/checkin/" << token << "',width:200,height:200});"
             << "}"
             << "</script>";
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // POST /events/<id>/generate-checkin — same for events
    CROW_ROUTE(app, "/events/<int>/generate-checkin").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) { res.code = 404; res.write("Not found"); return res; }

        auto& auth = app.get_context<AuthMiddleware>(req);
        bool can = auth.auth.can("schedule.all_chapters") || (ev->event_lead_id == auth.auth.member_id);
        if (!can && ev->chapter_id > 0) {
            can = can_manage_chapter_content(req, res, app, ev->chapter_id, chapter_members);
            if (!can) return res;
        }
        if (!can) { res.code = 403; res.write("Forbidden"); return res; }

        std::string token = ev->checkin_token;
        if (token.empty()) {
            token = EventService::generate_uuid();
            event_repo.update_checkin_token(ev->id, token);
        }

        std::ostringstream html;
        html << "<div class=\"text-center p-4\">"
             << "<div id=\"checkin-qr\" class=\"inline-block bg-white p-4 rounded-xl border border-gray-200\"></div>"
             << "<p class=\"text-xs text-gray-500 mt-3\">Scan to check in to this event</p>"
             << "<input type=\"text\" readonly value=\"" << token << "\" "
             << "class=\"mt-2 w-full text-center text-xs font-mono bg-gray-50 border border-gray-200 rounded px-2 py-1 select-all\">"
             << "</div>"
             << "<script>"
             << "if(typeof QRCode!=='undefined'){"
             << "document.getElementById('checkin-qr').innerHTML='';"
             << "new QRCode(document.getElementById('checkin-qr'),{text:window.location.origin+'/checkin/" << token << "',width:200,height:200});"
             << "}"
             << "</script>";
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // GET /checkin/<token> — public check-in page (no auth required)
    CROW_ROUTE(app, "/checkin/<str>")([&](const crow::request& req, const std::string& token) {
        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        auto target = resolve_checkin(token, meeting_repo, event_repo, attendance);
        if (!target) {
            res.code = 404;
            auto tmpl = crow::mustache::load("checkin/_page.html");
            crow::mustache::context ctx;
            ctx["not_found"] = true;
            ctx["asset_v"] = asset_version();
            res.write(tmpl.render(ctx).dump());
            return res;
        }
        const std::string& entity_type     = target->entity_type;
        const int64_t      entity_id       = target->entity_id;
        const std::string& entity_title    = target->title;
        const std::string  entity_date     = friendly_date(target->date);
        const std::string& entity_location = target->location;

        // Check if user just came back from Discord OAuth
        auto qs = crow::query_string(req.url_params);
        const char* discord_flag = qs.get("discord");
        auto& auth_ctx = app.get_context<AuthMiddleware>(req);
        std::string checkin_msg;

        bool own_login = get_cookie(req, "checkin_ok") == token;
        if (own_login)
            res.add_header("Set-Cookie", "checkin_ok=; HttpOnly; Path=/checkin; Max-Age=0; SameSite=Lax");
        if (discord_flag && own_login && auth_ctx.auth.authenticated) {
            // Auto check-in the Discord-authenticated user
            int64_t mbr_id = auth_ctx.auth.member_id;
            if (attendance.is_checked_in(mbr_id, entity_type, entity_id)) {
                checkin_msg = "You're already checked in!";
            } else {
                attendance.check_in(mbr_id, entity_type, entity_id);
                audit.log_system("checkin.discord", entity_type, entity_id, entity_title, "Discord check-in: " + auth_ctx.auth.display_name);
                checkin_msg = "Welcome, " + auth_ctx.auth.display_name + "! You're checked in.";
            }
        }

        const char* error_flag = qs.get("error");

        crow::mustache::context ctx;
        ctx["token"] = token;
        ctx["entity_type"] = entity_type;
        ctx["entity_id"] = entity_id;
        ctx["entity_title"] = entity_title;
        ctx["entity_date"] = entity_date;
        ctx["entity_location"] = entity_location;
        ctx["is_meeting"] = (entity_type == "meeting");

        if (!checkin_msg.empty()) {
            ctx["checkin_success"] = true;
            ctx["checkin_msg"] = checkin_msg;
        }
        if (error_flag) {
            ctx["checkin_error"] = true;
            ctx["checkin_error_msg"] = std::string("Discord login failed. Please try another method.");
        }

        // Build Discord OAuth URL with checkin state
        // /auth/login mints the CSRF state nonce + cookie and carries the
        // token through OAuth so the callback lands back here.
        ctx["discord_oauth_url"] = "/auth/login?checkin=" + token;

        ctx["asset_v"] = asset_version();
        auto tmpl = crow::mustache::load("checkin/_page.html");
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // POST /checkin/<token>/select — check in by selecting from member list
    CROW_ROUTE(app, "/checkin/<str>/select").methods("POST"_method)(
        [&](const crow::request& req, const std::string& token) {
        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        auto params = crow::query_string("?" + req.body);
        const char* mid_raw = params.get("member_id");
        if (!mid_raw || std::string(mid_raw).empty()) {
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">Please select a member.</div>)");
            return res;
        }
        int64_t member_id = 0;
        try { member_id = std::stoll(mid_raw); } catch (...) {}

        // Verify the member exists
        auto member = member_repo.find_by_id(member_id);
        if (!member) {
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">Member not found.</div>)");
            return res;
        }

        // Find entity
        auto target = resolve_checkin(token, meeting_repo, event_repo, attendance);
        if (!target) {
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">This check-in link is not active.</div>)");
            return res;
        }
        const std::string& entity_type  = target->entity_type;
        const std::string& entity_title = target->title;
        const int64_t      entity_id    = target->entity_id;

        // Check for duplicate
        if (attendance.is_checked_in(member_id, entity_type, entity_id)) {
            res.write(R"(<div class="bg-yellow-50 border border-yellow-200 text-yellow-700 px-4 py-3 rounded text-sm">)"
                      + html_escape(member->display_name) + " is already checked in!</div>");
            return res;
        }

        const char* virt_raw = params.get("is_virtual");
        bool is_virtual = virt_raw && std::string(virt_raw) == "1";
        attendance.check_in(member_id, entity_type, entity_id, "", is_virtual);
        audit.log_system("checkin.select", entity_type, entity_id, entity_title, "Select check-in: " + member->display_name);

        res.write("<div class=\"bg-green-50 border border-green-200 text-green-700 px-4 py-3 rounded text-sm\">"
                  + html_escape(member->display_name) + " checked in successfully!</div>");
        return res;
    });

    // POST /checkin/<token>/manual — manual entry for new member
    CROW_ROUTE(app, "/checkin/<str>/manual").methods("POST"_method)(
        [&](const crow::request& req, const std::string& token) {
        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        std::string first = gp("first_name");
        std::string last = gp("last_name");
        if (first.empty() || last.empty()) {
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">First and last name are required.</div>)");
            return res;
        }

        // Find entity
        auto target = resolve_checkin(token, meeting_repo, event_repo, attendance);
        if (!target) {
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">This check-in link is not active.</div>)");
            return res;
        }
        const std::string& entity_type  = target->entity_type;
        const std::string& entity_title = target->title;
        const int64_t      entity_id    = target->entity_id;

        // Duplicate detection: search for existing members with same first+last name
        // Indexed exact-name lookup (was: load every member and compare in C++).
        if (auto found = member_repo.find_by_full_name(first, last)) {
            const Member& m = *found;
            {
                // Found a match — check if already checked in
                if (attendance.is_checked_in(m.id, entity_type, entity_id)) {
                    res.write("<div class=\"bg-yellow-50 border border-yellow-200 text-yellow-700 px-4 py-3 rounded text-sm\">"
                              + html_escape(m.display_name) + " is already checked in!</div>");
                    return res;
                }
                // Check them in
                const char* virt_raw = params.get("is_virtual");
                bool is_virtual = virt_raw && std::string(virt_raw) == "1";
                attendance.check_in(m.id, entity_type, entity_id, "", is_virtual);
                audit.log_system("checkin.manual", entity_type, entity_id, entity_title, "Manual check-in (existing): " + m.display_name);
                res.write("<div class=\"bg-green-50 border border-green-200 text-green-700 px-4 py-3 rounded text-sm\">"
                          "Welcome back, " + html_escape(m.display_name) + "! Checked in successfully.</div>");
                return res;
            }
        }

        // No match — create new member and check in
        static NewMemberLimiter new_member_limiter;
        if (!new_member_limiter.allow(token)) {
            res.code = 429;
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">Too many new sign-ups on this link right now. Please ask an organizer to check you in.</div>)");
            return res;
        }
        Member newm;
        newm.first_name = first;
        newm.last_name = last;
        newm.email = gp("email");
        auto created = members.create(newm);

        const char* virt_raw = params.get("is_virtual");
        bool is_virtual = virt_raw && std::string(virt_raw) == "1";
        attendance.check_in(created.id, entity_type, entity_id, "", is_virtual);
        audit.log_system("checkin.manual", entity_type, entity_id, entity_title, "Manual check-in (new member): " + created.display_name);

        res.write("<div class=\"bg-green-50 border border-green-200 text-green-700 px-4 py-3 rounded text-sm\">"
                  "Welcome, " + html_escape(created.display_name) + "! You've been checked in.</div>");
        return res;
    });

    // GET /checkin/<token>/search?q= — search members for the select dropdown
    CROW_ROUTE(app, "/checkin/<str>/search")([&](const crow::request& req, const std::string& token) {
        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        // Only answer for a token that currently resolves to a check-in the
        // same way GET /checkin/<token> does - this route is unauthenticated,
        // so without the check it was an open member-directory search.
        bool token_ok = resolve_checkin(token, meeting_repo, event_repo, attendance).has_value();
        if (!token_ok) {
            res.code = 404;
            res.write(R"(<option value="">Invalid check-in link</option>)");
            return res;
        }

        auto qs = crow::query_string(req.url_params);
        const char* q_raw = qs.get("q");
        std::string q = q_raw ? q_raw : "";

        if (q.size() < 2) {
            res.write(R"(<option value="">Type at least 2 characters...</option>)");
            return res;
        }

        // Name fields only, capped: this page is public, so it must not let
        // anyone probe by email/Discord handle or dump the whole list.
        auto results = member_repo.search_names(q, 15);
        std::ostringstream html;
        html << "<option value=\"\">-- Select your name --</option>\n";
        for (const auto& m : results) {
            html << "<option value=\"" << m.id << "\">"
                 << html_escape(m.display_name);
            if (!m.first_name.empty())
                html << " (" << html_escape(m.first_name) << " " << html_escape(m.last_name) << ")";
            html << "</option>\n";
        }
        if (results.empty()) {
            html << "<option value=\"\" disabled>No members found</option>\n";
        }
        res.write(html.str());
        return res;
    });

    // GET /auth/callback handles Discord OAuth — need to check for checkin: state
    // This is handled in AuthRoutes already, but we need to add checkin handling there.
    // For now, we'll add a dedicated callback route.
    // POST /checkin/<token>/discord-callback?code=... — after Discord OAuth redirect
    // Actually, the OAuth callback URL is fixed (/auth/callback). We encode the token
    // in the state parameter. The existing AuthRoutes callback needs to detect "checkin:TOKEN"
    // state and redirect to complete check-in. Let me add that logic separately.

    // ── Kiosk mode ────────────────────────────────────────────────────────
    // Full-screen page for a tablet/projector at the venue: big QR code for
    // the check-in link plus a live count and the latest arrivals. Managers
    // only (it mints the check-in token if needed).
    auto render_kiosk = [&](const crow::request& req, const std::string& type, int64_t id,
                            const std::string& title, const std::string& when, const std::string& token) {
        crow::mustache::context ctx;
        ctx["entity_type"] = type;
        ctx["entity_id"]   = id;
        ctx["title"]       = title;
        ctx["when"]        = when;
        ctx["token"]       = token;
        (void)req;
        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        ctx["asset_v"] = asset_version();
        res.write(crow::mustache::load("checkin/_kiosk.html").render(ctx).dump());
        return res;
    };

    CROW_ROUTE(app, "/meetings/<int>/kiosk")([&, render_kiosk](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto mtg = meetings.get(id);
        if (!mtg) { res.code = 404; return res; }
        if (!can_manage_chapter_content(req, res, app, mtg->chapter_id, chapter_members)) return res;
        std::string token = mtg->checkin_token;
        if (token.empty()) {
            token = MeetingService::generate_uuid();
            meeting_repo.update_checkin_token(mtg->id, token);
        }
        return render_kiosk(req, "meeting", mtg->id, mtg->title, mtg->start_time, token);
    });

    CROW_ROUTE(app, "/events/<int>/kiosk")([&, render_kiosk](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        std::string token = ev->checkin_token;
        if (token.empty()) {
            token = EventService::generate_uuid();
            event_repo.update_checkin_token(ev->id, token);
        }
        return render_kiosk(req, "event", ev->id, ev->title, ev->start_time, token);
    });

    // GET /kiosk/<type>/<id>/recent - latest arrivals for the kiosk (polled)
    CROW_ROUTE(app, "/kiosk/<str>/<int>/recent")([&](const crow::request& req, std::string type, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        if (type != "meeting" && type != "event") { res.code = 404; return res; }
        if (type == "event") {
            auto ev = events.get(id);
            if (!ev || !can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        } else {
            auto mtg = meetings.get(id);
            if (!mtg) { res.code = 404; return res; }
            if (!can_manage_chapter_content(req, res, app, mtg->chapter_id, chapter_members)) return res;
        }
        auto rows = attendance.get_attendees(type, id);
        std::sort(rows.begin(), rows.end(), [](const Attendance& a, const Attendance& b) {
            return a.checked_in_at > b.checked_in_at;
        });
        std::ostringstream html;
        html << "<div class=\"text-6xl font-bold text-gray-900\">" << attendance.get_count(type, id)
             << "</div><div class=\"text-gray-500 mb-6\">checked in</div><ul class=\"space-y-2 text-2xl\">";
        for (size_t i = 0; i < rows.size() && i < 6; ++i)
            html << "<li class=\"text-gray-800\">\u2705 " << html_escape(rows[i].member_display_name) << "</li>";
        html << "</ul>";
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // ── Public-show visitor counter ─────────────────────────────────────────
    // Phone-friendly tap counter for organizers at a public show; fills the
    // event's public kids/teens/adults totals used in reports.
    auto counter_html = [](int64_t id, const EventRepository::Visitors& v) {
        auto btn = [&](const char* kind, const char* label, int n, const char* color) {
            std::string k = kind;
            return "<div class=\"flex items-center gap-3\">"
                   "<button hx-post=\"/events/" + std::to_string(id) + "/visitors\" hx-vals='{\"kind\":\"" + k +
                   "\",\"delta\":\"-1\"}' hx-target=\"#visitor-counter\" hx-swap=\"outerHTML\" "
                   "aria-label=\"Undo one " + label + "\" class=\"w-14 h-14 rounded-xl border border-gray-300 text-2xl text-gray-600\">&minus;</button>"
                   "<button hx-post=\"/events/" + std::to_string(id) + "/visitors\" hx-vals='{\"kind\":\"" + k +
                   "\",\"delta\":\"1\"}' hx-target=\"#visitor-counter\" hx-swap=\"outerHTML\" "
                   "class=\"flex-1 h-20 rounded-xl " + std::string(color) + " text-gray-900 text-2xl font-bold\">" +
                   label + " <span class=\"ml-2\">" + std::to_string(n) + "</span></button></div>";
        };
        return "<div id=\"visitor-counter\" class=\"space-y-3\">" +
               btn("kids", "Kids", v.kids, "bg-yellow-300") + btn("teens", "Teens", v.teens, "bg-sky-300") +
               btn("adults", "Adults", v.adults, "bg-green-300") +
               "<p class=\"text-center text-gray-500\">Total " + std::to_string(v.kids + v.teens + v.adults) + "</p></div>";
    };

    CROW_ROUTE(app, "/events/<int>/counter")([&, counter_html](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        crow::mustache::context ctx;
        ctx["title"] = ev->title;
        ctx["asset_v"] = asset_version();
        ctx["counter"] = counter_html(ev->id, {ev->public_kids, ev->public_teens, ev->public_adults});
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(crow::mustache::load("checkin/_counter.html").render(ctx).dump());
        return res;
    });

    CROW_ROUTE(app, "/events/<int>/visitors").methods("POST"_method)([&, counter_html](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        auto params = crow::query_string("?" + req.body);
        std::string kind = params.get("kind") ? params.get("kind") : "";
        std::string d = params.get("delta") ? params.get("delta") : "";
        if ((kind != "kids" && kind != "teens" && kind != "adults") || (d != "1" && d != "-1")) {
            res.code = 400;
            return res;
        }
        auto v = event_repo.add_visitors(ev->id, kind, d == "1" ? 1 : -1);
        live::changed_by(req, "event", ev->id);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(counter_html(ev->id, v));
        return res;
    });
}
