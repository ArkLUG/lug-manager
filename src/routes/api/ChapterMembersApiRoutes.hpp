#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "services/ChapterService.hpp"
#include "integrations/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_chapter_members_api_routes(LugApp& app, ChapterMemberRepository& chapter_members,
                                          ChapterService& chapters, MemberRepository& member_repo,
                                          DiscordClient& discord, AuditService& audit);
