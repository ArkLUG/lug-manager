#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/PhotoStore.hpp"
#include "services/EventService.hpp"
#include "services/AuditService.hpp"
#include "integrations/DiscordClient.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include <memory>

// /uploads/<name> (members only), event photo galleries and build challenges.
void register_gallery_routes(LugApp& app, SqliteDatabase& db, std::shared_ptr<PhotoStore> photos,
                             EventService& events, ChapterMemberRepository& chapter_members,
                             DiscordClient& discord, AuditService& audit);
