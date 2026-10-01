#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "chat/ChatHub.hpp"
#include "services/AuditService.hpp"
#include <memory>

class Notifier;

// Admin pages for chat integrations:
//   /settings/messages[/<key>]   message wording (templates): edit, preview, reset, test
//   /settings/chat-activity      what was posted/messaged, failures, retry
void register_chat_settings_routes(LugApp& app, SqliteDatabase& db, std::shared_ptr<chat::ChatHub> hub,
                                   std::shared_ptr<Notifier> notifier, AuditService& audit);
