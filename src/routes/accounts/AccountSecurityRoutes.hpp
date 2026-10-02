#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "services/AuditService.hpp"
#include "services/notifications/Notifier.hpp"
#include <memory>

// Password and two-factor management (the sign-in steps themselves are in
// AuthRoutes.cpp):
//   /account/security, /account/password, /account/2fa/{setup,enable,disable,recovery}
//   /settings/sign-in (admin): password sign-in, email links, who must use 2FA
//   /members/<id>/password-link, /members/<id>/reset-2fa (admin)
// A member changed their own email: until they click the link sent to it,
// it can't sign in or get mail. Without email set up (SMTP + LUG_PUBLIC_URL)
// there's no way to confirm, so it counts as confirmed. Returns true if a
// link went out. Rate-limited (3 an hour).
bool request_email_confirmation(SqliteDatabase& db, Notifier* notifier, const std::string& public_url, int64_t member_id);

void register_account_security_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings, AuthService& auth,
                                      std::shared_ptr<Notifier> notifier, AuditService& audit, const std::string& public_url);
