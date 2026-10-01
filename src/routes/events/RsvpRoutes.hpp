#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/events/RsvpRepository.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "services/events/EventService.hpp"
#include "services/AuditService.hpp"
#include "services/notifications/Notifier.hpp"
#include <memory>

// Is RSVP open for this event right now? Not cancelled, not over, and the
// signup deadline (date or datetime, LUG-local) hasn't passed.
bool rsvp_open(const LugEvent& ev);

void register_rsvp_routes(LugApp& app, EventService& events,
                          std::shared_ptr<RsvpRepository> rsvps,
                          ChapterMemberRepository& chapter_members, AuditService& audit,
                          std::shared_ptr<Notifier> notifier);
