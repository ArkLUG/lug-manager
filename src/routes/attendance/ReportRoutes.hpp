#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/events/EventService.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "repositories/events/DisplayRequestRepository.hpp"
#include "repositories/events/EventDayRepository.hpp"
#include "repositories/events/EventDayAttendanceRepository.hpp"
#include "services/AuditService.hpp"
#include <memory>

// /reports/annual?year= (admin): the LUG's year in numbers.
// /events/<id>/report (event managers): printable post-event report in the
// shape LEGO Fan CoLab (formerly the LEGO Ambassador Network) asks for.
void register_report_routes(LugApp& app, SqliteDatabase& db, EventService& events,
                            EventDayRepository& days, EventDayAttendanceRepository& day_att,
                            std::shared_ptr<DisplayRequestRepository> displays,
                            ChapterMemberRepository& chapter_members, AuditService& audit);
