#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/ChapterMemberRepository.hpp"

void register_help_routes(LugApp& app, ChapterMemberRepository& chapter_members);
