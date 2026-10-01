#pragma once

#include <crow.h>
#include "services/members/ChapterService.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "services/members/MemberService.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "services/AuditService.hpp"

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_chapter_routes(LugApp& app, ChapterService& chapters,
                              ChapterMemberRepository& chapter_members,
                              MemberService& members,
                              DiscordClient& discord, AuditService& audit);
