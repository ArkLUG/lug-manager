#include "routes/AuditRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>

static constexpr int kAuditPerPage = 50;

void register_audit_routes(LugApp& app, AuditService& audit) {

    // GET /audit — admin-only audit log viewer
    CROW_ROUTE(app, "/audit")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto qs = crow::query_string(req.url_params);
        const char* s_raw = qs.get("search");
        const char* af_raw = qs.get("action_filter");
        std::string search = s_raw ? s_raw : "";
        std::string action_filter = af_raw ? af_raw : "";

        int page = 1;
        { const char* p = qs.get("page"); if (p) try { page = std::stoi(p); } catch (...) {} }
        if (page < 1) page = 1;

        int total = audit.repo().count_filtered(search, action_filter);
        int total_pages = (total + kAuditPerPage - 1) / kAuditPerPage;
        if (total_pages < 1) total_pages = 1;
        if (page > total_pages) page = total_pages;
        int offset = (page - 1) * kAuditPerPage;

        auto entries = audit.repo().find_paginated(search, action_filter, kAuditPerPage, offset);

        crow::mustache::context ctx;
        ctx["search"]         = search;
        ctx["action_filter"]  = action_filter;
        ctx["page"]           = page;
        ctx["total_pages"]    = total_pages;
        ctx["total_count"]    = total;
        ctx["has_prev"]       = (page > 1);
        ctx["has_next"]       = (page < total_pages);
        ctx["prev_page"]      = page - 1;
        ctx["next_page"]      = page + 1;

        // Filter selections
        ctx["filter_member"]     = (action_filter == "member");
        ctx["filter_meeting"]    = (action_filter == "meeting");
        ctx["filter_event"]      = (action_filter == "event");
        ctx["filter_chapter"]    = (action_filter == "chapter");
        ctx["filter_attendance"] = (action_filter == "attendance");
        ctx["filter_settings"]   = (action_filter == "settings");
        ctx["filter_sync"]       = (action_filter == "sync");
        ctx["filter_checkin"]    = (action_filter == "checkin");

        crow::json::wvalue arr;
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            arr[i]["id"]          = e.id;
            arr[i]["actor_name"]  = e.actor_name;
            arr[i]["action"]      = e.action;
            arr[i]["entity_type"] = e.entity_type;
            arr[i]["entity_id"]   = e.entity_id;
            arr[i]["entity_name"] = e.entity_name;
            arr[i]["details"]     = e.details;
            arr[i]["ip_address"]  = e.ip_address;
            arr[i]["created_at"]  = e.created_at;

            // Action color classes
            bool is_create = e.action.find("create") != std::string::npos || e.action.find("checkin") != std::string::npos;
            bool is_delete = e.action.find("delete") != std::string::npos || e.action.find("remove") != std::string::npos;
            bool is_update = e.action.find("update") != std::string::npos || e.action.find("edit") != std::string::npos
                          || e.action.find("dues") != std::string::npos || e.action.find("status") != std::string::npos;
            arr[i]["action_is_create"] = is_create;
            arr[i]["action_is_delete"] = is_delete;
            arr[i]["action_is_update"] = is_update && !is_create && !is_delete;
            arr[i]["action_is_other"]  = !is_create && !is_delete && !is_update;
        }
        ctx["entries"] = std::move(arr);

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        auto content_tmpl = crow::mustache::load("settings/_audit.html");
        std::string content = content_tmpl.render(ctx).dump();

        if (is_htmx) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(content);
        } else {
            crow::mustache::context layout_ctx;
            layout_ctx["content"]         = content;
            layout_ctx["page_title"]      = "Audit Log";
            layout_ctx["active_audit"]    = true;
            layout_ctx["is_admin"]        = true;
            set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });
}
