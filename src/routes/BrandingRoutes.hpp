#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "repositories/SettingsRepository.hpp"
#include "services/AuditService.hpp"
#include <crow.h>
#include <string>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

// GET /settings/branding (page) + POST /settings/branding (upload) + GET
// /branding/logo (serves the uploaded file, used for both the sidebar logo
// and the favicon - see layout.html). data_dir is the directory the uploaded
// logo file is stored in; it's the same durable volume the SQLite DB lives
// in (see main.cpp), so it survives redeploys unlike src/static/, which is
// baked into the image and read-only at runtime.
void register_branding_routes(LugApp& app, SettingsRepository& settings,
                               AuditService& audit, const std::string& data_dir);
