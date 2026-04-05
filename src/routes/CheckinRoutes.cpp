#include "routes/CheckinRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <sstream>

void register_checkin_routes(LugApp& app,
                              MeetingRepository& meeting_repo,
                              EventRepository& event_repo,
                              MeetingService& meetings,
                              EventService& events,
                              AttendanceService& attendance,
                              MemberService& members,
                              MemberRepository& member_repo,
                              ChapterMemberRepository& chapter_members,
                              DiscordOAuth& oauth,
                              DiscordClient& discord,
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
        bool can = auth.auth.is_admin() || (ev->event_lead_id == auth.auth.member_id);
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

        // Look up meeting or event by token
        std::string entity_type, entity_title, entity_date, entity_location;
        int64_t entity_id = 0;
        bool is_virtual_meeting = false;

        auto mtg = meeting_repo.find_by_checkin_token(token);
        if (mtg) {
            if (mtg->is_virtual) {
                res.code = 404;
                auto tmpl = crow::mustache::load("checkin/_page.html");
                crow::mustache::context ctx;
                ctx["not_found"] = true;
                res.write(tmpl.render(ctx).dump());
                return res;
            }
            entity_type = "meeting";
            entity_id = mtg->id;
            entity_title = mtg->title;
            entity_date = mtg->start_time;
            entity_location = mtg->location;
        } else {
            auto ev = event_repo.find_by_checkin_token(token);
            if (ev) {
                entity_type = "event";
                entity_id = ev->id;
                entity_title = ev->title;
                entity_date = ev->start_time;
                entity_location = ev->location;
            }
        }

        if (entity_id == 0) {
            res.code = 404;
            auto tmpl = crow::mustache::load("checkin/_page.html");
            crow::mustache::context ctx;
            ctx["not_found"] = true;
            res.write(tmpl.render(ctx).dump());
            return res;
        }

        // Check if user just came back from Discord OAuth
        auto qs = crow::query_string(req.url_params);
        const char* discord_flag = qs.get("discord");
        auto& auth_ctx = app.get_context<AuthMiddleware>(req);
        std::string checkin_msg;

        if (discord_flag && auth_ctx.auth.authenticated) {
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
        ctx["is_virtual_meeting"] = is_virtual_meeting;

        if (!checkin_msg.empty()) {
            ctx["checkin_success"] = true;
            ctx["checkin_msg"] = checkin_msg;
        }
        if (error_flag) {
            ctx["checkin_error"] = true;
            ctx["checkin_error_msg"] = std::string("Discord login failed. Please try another method.");
        }

        // Build Discord OAuth URL with checkin state
        std::string redirect_uri;
        {
            std::string proto = req.get_header_value("X-Forwarded-Proto");
            if (proto.empty()) proto = "http";
            std::string host = req.get_header_value("X-Forwarded-Host");
            if (host.empty()) host = req.get_header_value("Host");
            if (host.empty()) host = "localhost";
            redirect_uri = proto + "://" + host + "/auth/callback";
        }
        ctx["discord_oauth_url"] = oauth.get_auth_url("checkin:" + token, redirect_uri);

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
        int64_t member_id = std::stoll(mid_raw);

        // Verify the member exists
        auto member = member_repo.find_by_id(member_id);
        if (!member) {
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">Member not found.</div>)");
            return res;
        }

        // Find entity
        std::string entity_type;
        std::string entity_title;
        int64_t entity_id = 0;
        auto mtg = meeting_repo.find_by_checkin_token(token);
        if (mtg) { entity_type = "meeting"; entity_id = mtg->id; entity_title = mtg->title; }
        else {
            auto ev = event_repo.find_by_checkin_token(token);
            if (ev) { entity_type = "event"; entity_id = ev->id; entity_title = ev->title; }
        }
        if (entity_id == 0) {
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">Invalid check-in link.</div>)");
            return res;
        }

        // Check for duplicate
        if (attendance.is_checked_in(member_id, entity_type, entity_id)) {
            res.write(R"(<div class="bg-yellow-50 border border-yellow-200 text-yellow-700 px-4 py-3 rounded text-sm">)"
                      + member->display_name + " is already checked in!</div>");
            return res;
        }

        const char* virt_raw = params.get("is_virtual");
        bool is_virtual = virt_raw && std::string(virt_raw) == "1";
        attendance.check_in(member_id, entity_type, entity_id, "", is_virtual);
        audit.log_system("checkin.select", entity_type, entity_id, entity_title, "Select check-in: " + member->display_name);

        res.write("<div class=\"bg-green-50 border border-green-200 text-green-700 px-4 py-3 rounded text-sm\">"
                  + member->display_name + " checked in successfully!</div>");
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
        std::string entity_type;
        std::string entity_title;
        int64_t entity_id = 0;
        auto mtg = meeting_repo.find_by_checkin_token(token);
        if (mtg) { entity_type = "meeting"; entity_id = mtg->id; entity_title = mtg->title; }
        else {
            auto ev = event_repo.find_by_checkin_token(token);
            if (ev) { entity_type = "event"; entity_id = ev->id; entity_title = ev->title; }
        }
        if (entity_id == 0) {
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded text-sm">Invalid check-in link.</div>)");
            return res;
        }

        // Duplicate detection: search for existing members with same first+last name
        auto all = member_repo.find_all();
        for (const auto& m : all) {
            if (strcasecmp(m.first_name.c_str(), first.c_str()) == 0 &&
                strcasecmp(m.last_name.c_str(), last.c_str()) == 0) {
                // Found a match — check if already checked in
                if (attendance.is_checked_in(m.id, entity_type, entity_id)) {
                    res.write("<div class=\"bg-yellow-50 border border-yellow-200 text-yellow-700 px-4 py-3 rounded text-sm\">"
                              + m.display_name + " is already checked in!</div>");
                    return res;
                }
                // Check them in
                const char* virt_raw = params.get("is_virtual");
                bool is_virtual = virt_raw && std::string(virt_raw) == "1";
                attendance.check_in(m.id, entity_type, entity_id, "", is_virtual);
                audit.log_system("checkin.manual", entity_type, entity_id, entity_title, "Manual check-in (existing): " + m.display_name);
                res.write("<div class=\"bg-green-50 border border-green-200 text-green-700 px-4 py-3 rounded text-sm\">"
                          "Welcome back, " + m.display_name + "! Checked in successfully.</div>");
                return res;
            }
        }

        // No match — create new member and check in
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
                  "Welcome, " + created.display_name + "! You've been checked in.</div>");
        return res;
    });

    // GET /checkin/<token>/search?q= — search members for the select dropdown
    CROW_ROUTE(app, "/checkin/<str>/search")([&](const crow::request& req, const std::string& token) {
        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        auto qs = crow::query_string(req.url_params);
        const char* q_raw = qs.get("q");
        std::string q = q_raw ? q_raw : "";

        if (q.size() < 2) {
            res.write(R"(<option value="">Type at least 2 characters...</option>)");
            return res;
        }

        auto results = member_repo.find_search(q);
        std::ostringstream html;
        html << "<option value=\"\">-- Select your name --</option>\n";
        for (const auto& m : results) {
            html << "<option value=\"" << m.id << "\">"
                 << m.display_name;
            if (!m.first_name.empty())
                html << " (" << m.first_name << " " << m.last_name << ")";
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
}
