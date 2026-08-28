#pragma once
#include "repositories/ApiKeyRepository.hpp"
#include "utils/Crypto.hpp"
#include <crow.h>
#include <string>

struct ApiKeyContext {
    bool        authenticated = false;
    int64_t     key_id        = 0;
    std::string label;
    std::string scope;      // "read" | "write" | "admin"

    bool has_scope(const std::string& required) const {
        static const auto rank = [](const std::string& s) {
            if (s == "admin") return 3;
            if (s == "write") return 2;
            if (s == "read")  return 1;
            return 0;
        };
        return rank(scope) >= rank(required);
    }
};

// Crow middleware that reads X-API-Key (or "Authorization: Bearer <key>") header,
// hashes it, and looks it up. Does NOT block requests itself - routes decide via
// require_api_scope(). Mirrors AuthMiddleware's "populate context, let routes gate" design.
struct ApiKeyMiddleware {
    ApiKeyRepository* api_keys = nullptr; // Set before server starts

    struct context {
        ApiKeyContext api_key;
    };

    void before_handle(crow::request& req, crow::response& /*res*/, context& ctx) {
        if (!api_keys) return;

        std::string raw = req.get_header_value("X-API-Key");
        if (raw.empty()) {
            std::string auth_hdr = req.get_header_value("Authorization");
            const std::string prefix = "Bearer ";
            if (auth_hdr.rfind(prefix, 0) == 0) raw = auth_hdr.substr(prefix.size());
        }
        if (raw.empty()) return;

        std::string hash = sha256_hex(raw);
        auto key_opt = api_keys->find_by_hash(hash);
        if (!key_opt) return;

        ctx.api_key.authenticated = true;
        ctx.api_key.key_id        = key_opt->id;
        ctx.api_key.label         = key_opt->label;
        ctx.api_key.scope         = key_opt->scope;

        api_keys->touch_last_used(key_opt->id); // best-effort; fine if this races
    }

    void after_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/) {}
};

// Helper: check API scope in route handlers. Returns false and writes the standard
// JSON error envelope if unauthenticated/insufficient scope.
template<typename App>
inline bool require_api_scope(const crow::request& req, crow::response& res, App& app,
                               const std::string& min_scope) {
    auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
    if (!ctx.api_key.authenticated) {
        res.code = 401;
        res.add_header("Content-Type", "application/json");
        res.write(R"({"error":"invalid or missing API key","code":"unauthenticated"})");
        return false;
    }
    if (!ctx.api_key.has_scope(min_scope)) {
        res.code = 403;
        res.add_header("Content-Type", "application/json");
        res.write(R"({"error":"insufficient API key scope","code":"forbidden"})");
        return false;
    }
    return true;
}
