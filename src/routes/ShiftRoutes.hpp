#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/ShiftRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "services/EventService.hpp"
#include "services/AuditService.hpp"
#include <memory>

void register_shift_routes(LugApp& app, EventService& events, std::shared_ptr<ShiftRepository> shifts,
                           ChapterMemberRepository& chapter_members, AuditService& audit);
