#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/PhotoStore.hpp"
#include "services/events/EventService.hpp"
#include "services/AuditService.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include <memory>

// /uploads/<name> (members only), event photo galleries and build challenges.
void register_gallery_routes(LugApp& app, SqliteDatabase& db, std::shared_ptr<PhotoStore> photos,
                             EventService& events, ChapterMemberRepository& chapter_members,
                             AuditService& audit);
