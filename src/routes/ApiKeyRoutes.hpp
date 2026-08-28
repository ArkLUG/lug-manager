#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "repositories/ApiKeyRepository.hpp"
#include "services/AuditService.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_api_key_routes(LugApp& app, ApiKeyRepository& api_keys, AuditService& audit);
