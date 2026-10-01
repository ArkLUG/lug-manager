#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"

void register_help_routes(LugApp& app, ChapterMemberRepository& chapter_members);
