#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/events/DisplayRequestRepository.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "services/events/EventService.hpp"
#include "services/AuditService.hpp"
#include <memory>

void register_display_routes(LugApp& app, EventService& events,
                             std::shared_ptr<DisplayRequestRepository> displays,
                             ChapterMemberRepository& chapter_members, AuditService& audit);
