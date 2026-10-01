#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/events/SeriesService.hpp"
#include "services/AuditService.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "services/members/ChapterService.hpp"
#include <memory>

void register_series_routes(LugApp& app, std::shared_ptr<SeriesService> series,
                            ChapterService& chapters, ChapterMemberRepository& chapter_members,
                            AuditService& audit);
