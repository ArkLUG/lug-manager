#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "services/AuditService.hpp"

void register_chapter_members_api_routes(LugApp& app, ChapterMemberRepository& chapter_members,
                                          AuditService& audit);
