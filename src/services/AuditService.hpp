#pragma once
#include "live/LiveHub.hpp"
#include "utils/web/ClientIp.hpp"
#include "repositories/admin/AuditLogRepository.hpp"
#include "middleware/AuthMiddleware.hpp"
#include <crow.h>
#include <string>

// Lightweight audit helper — call audit.log(...) from any route. Every logged
// change is also announced to open pages (live/LiveHub.hpp), so a route that
// logs its change needs nothing more to update other people's screens.
class AuditService {
public:
    explicit AuditService(AuditLogRepository& repo) : repo_(repo) {}

    // Log from an authenticated request
    template<typename App>
    void log(const crow::request& req, App& app,
             const std::string& action,
             const std::string& entity_type, int64_t entity_id,
             const std::string& entity_name,
             const std::string& details = "") {
        auto& ctx = app.template get_context<AuthMiddleware>(req);
        repo_.log(ctx.auth.member_id, ctx.auth.display_name,
                  action, entity_type, entity_id, entity_name, details, client_ip(req));
        const std::string tab = req.get_header_value("X-Live-Tab");
        live::changed(entity_type, entity_id, tab);
        live::changed("audit", 0, tab);
    }

    // Log from a public/system context (no auth)
    void log_system(const std::string& action,
                    const std::string& entity_type, int64_t entity_id,
                    const std::string& entity_name,
                    const std::string& details = "",
                    const std::string& ip = "") {
        repo_.log(0, "system", action, entity_type, entity_id, entity_name, details, ip);
        live::changed(entity_type, entity_id);
        live::changed("audit");
    }

    AuditLogRepository& repo() { return repo_; }

private:
    AuditLogRepository& repo_;
};
