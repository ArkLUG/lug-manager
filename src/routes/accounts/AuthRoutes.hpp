#pragma once
#include <crow.h>
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "auth/AuthService.hpp"
#include "integrations/discord/DiscordOAuth.hpp"

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

// Absolute URLs use the site's public address (services/SiteSettings.hpp);
// unset = derived from the request (redirects only, never emailed links).
void register_auth_routes(LugApp& app, AuthService& auth, DiscordOAuth& oauth);

class Notifier;
class AuditService;
// Email sign-in links for members without Discord (only when email is
// configured) and the no-login /unsubscribe/<token> pages used by emails.
void register_email_auth_routes(LugApp& app, AuthService& auth, SqliteDatabase& db,
                                std::shared_ptr<Notifier> notifier, AuditService& audit);
