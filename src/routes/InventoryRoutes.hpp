#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/AuditService.hpp"
#include "services/EventService.hpp"
#include "services/PhotoStore.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include <memory>

// LUG inventory: items with quantities, checked out to members and returned.
// Everyone can see it; chapter leads and admins manage it.
// Also: item photos and condition, and per-event pack lists (/events/<id>/pack).
void register_inventory_routes(LugApp& app, SqliteDatabase& db, AuditService& audit,
                               std::shared_ptr<PhotoStore> photos, EventService& events,
                               ChapterMemberRepository& chapter_members);
