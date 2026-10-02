#pragma once
#include "integrations/email/Mailer.hpp"
#include "routes/accounts/AuthRoutes.hpp"
#include "services/AuditService.hpp"
#include <memory>

// Settings > Email & address (admin): the site's public address (used in
// emailed links, calendar and chat links) and the SMTP server for email.
// Environment variables, when set, win and show as locked here; the SMTP
// password is environment-only (services/SiteSettings.hpp).
void register_site_settings_routes(LugApp& app, SettingsRepository& settings, std::shared_ptr<Mailer> mailer,
                                   AuditService& audit);
