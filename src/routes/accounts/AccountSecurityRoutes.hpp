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
void register_account_security_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings, AuthService& auth,
                                      std::shared_ptr<Notifier> notifier, AuditService& audit, const std::string& public_url);
