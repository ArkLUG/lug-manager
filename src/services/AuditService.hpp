#pragma once
#include "repositories/AuditLogRepository.hpp"
#include "middleware/AuthMiddleware.hpp"
#include <crow.h>
#include <string>

// Lightweight audit helper — call audit.log(...) from any route
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
        std::string ip = req.get_header_value("X-Forwarded-For");
        if (ip.empty()) ip = req.remote_ip_address;
        repo_.log(ctx.auth.member_id, ctx.auth.display_name,
                  action, entity_type, entity_id, entity_name, details, ip);
    }

    // Log from a public/system context (no auth)
    void log_system(const std::string& action,
                    const std::string& entity_type, int64_t entity_id,
                    const std::string& entity_name,
                    const std::string& details = "",
                    const std::string& ip = "") {
        repo_.log(0, "system", action, entity_type, entity_id, entity_name, details, ip);
    }

    AuditLogRepository& repo() { return repo_; }

private:
    AuditLogRepository& repo_;
};
