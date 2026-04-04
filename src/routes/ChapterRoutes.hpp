#pragma once

#include <crow.h>
#include "services/ChapterService.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "services/MemberService.hpp"
#include "integrations/DiscordClient.hpp"
#include "middleware/AuthMiddleware.hpp"
#include "services/AuditService.hpp"

using LugApp = crow::App<AuthMiddleware>;

void register_chapter_routes(LugApp& app, ChapterService& chapters,
                              ChapterMemberRepository& chapter_members,
                              MemberService& members,
                              DiscordClient& discord, AuditService& audit);
