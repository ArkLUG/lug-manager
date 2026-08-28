#pragma once
#include "middleware/ApiKeyMiddleware.hpp"
#include <crow.h>
#include <string>

// Shared helpers for the /api/v1/* JSON CRUD surface. See Section 3 of the
// implementation plan for the envelope/error-format spec these implement.

// Wraps a single entity or array into the standard success envelope: {"data": ...}
inline crow::json::wvalue envelope_ok(crow::json::wvalue data) {
    crow::json::wvalue out;
    out["data"] = std::move(data);
    return out;
}

// Wraps a list + pagination metadata: {"data": [...], "meta": {"total":,"limit":,"offset":}}
inline crow::json::wvalue envelope_list(crow::json::wvalue data, int total, int limit, int offset) {
    crow::json::wvalue out;
    out["data"]         = std::move(data);
    out["meta"]["total"]  = total;
    out["meta"]["limit"]  = limit;
    out["meta"]["offset"] = offset;
    return out;
}

// Writes the standard error envelope into res: {"error":"...","code":"..."}
inline void envelope_error(crow::response& res, int code, const std::string& msg,
                            const std::string& err_code) {
    res.code = code;
    res.add_header("Content-Type", "application/json");
    crow::json::wvalue body;
    body["error"] = msg;
    body["code"]  = err_code;
    res.write(body.dump());
}

// Writes a successful JSON response (200/201) with the given envelope body.
inline void write_json(crow::response& res, int code, crow::json::wvalue body) {
    res.code = code;
    res.add_header("Content-Type", "application/json");
    res.write(body.dump());
}

struct Pagination {
    int limit;
    int offset;
};

// Reads ?limit=&offset= query params, clamped to [1, max_limit] / [0, INT_MAX].
inline Pagination parse_pagination(const crow::request& req, int default_limit, int max_limit) {
    Pagination p{default_limit, 0};
    const char* limit_str  = req.url_params.get("limit");
    const char* offset_str = req.url_params.get("offset");
    if (limit_str) {
        try {
            int v = std::stoi(std::string(limit_str));
            if (v < 1) v = 1;
            if (v > max_limit) v = max_limit;
            p.limit = v;
        } catch (...) {}
    }
    if (offset_str) {
        try {
            int v = std::stoi(std::string(offset_str));
            if (v < 0) v = 0;
            p.offset = v;
        } catch (...) {}
    }
    return p;
}

// Actor label passed to AuditService::log_system for mutations made via the API,
// so the audit trail can attribute the change to a specific key.
inline std::string actor_label(const ApiKeyContext& ctx) {
    return "api:" + ctx.label;
}
