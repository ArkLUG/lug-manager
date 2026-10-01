#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/DisplayRequestRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "services/EventService.hpp"
#include "services/AuditService.hpp"
#include <memory>

void register_display_routes(LugApp& app, EventService& events,
                             std::shared_ptr<DisplayRequestRepository> displays,
                             ChapterMemberRepository& chapter_members, AuditService& audit);
