#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "services/members/ChapterService.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_chapter_members_api_routes(LugApp& app, ChapterMemberRepository& chapter_members,
                                          ChapterService& chapters, MemberRepository& member_repo,
                                          DiscordClient& discord, AuditService& audit);
