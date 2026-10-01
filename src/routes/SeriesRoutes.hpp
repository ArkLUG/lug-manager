#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/SeriesService.hpp"
#include "services/AuditService.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "services/ChapterService.hpp"
#include <memory>

void register_series_routes(LugApp& app, std::shared_ptr<SeriesService> series,
                            ChapterService& chapters, ChapterMemberRepository& chapter_members,
                            AuditService& audit);
