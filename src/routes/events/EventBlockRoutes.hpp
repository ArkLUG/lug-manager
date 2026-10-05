#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "services/events/EventService.hpp"
#include "services/AuditService.hpp"

// A show's day-by-day hours (repositories/events/EventBlocks.hpp): the panel on
// the event page, adding and removing blocks, and "same public hours every
// day". Changes re-sync the event's days (check-in), and its Discord event
// and calendar entries, without posting an "updated" note.
void register_event_block_routes(LugApp& app, SqliteDatabase& db, EventService& events,
                                 ChapterMemberRepository& chapter_members, AuditService& audit);
