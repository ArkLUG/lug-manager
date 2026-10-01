#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/events/ShiftRepository.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "services/events/EventService.hpp"
#include "services/AuditService.hpp"
#include <memory>

void register_shift_routes(LugApp& app, EventService& events, std::shared_ptr<ShiftRepository> shifts,
                           ChapterMemberRepository& chapter_members, AuditService& audit);
