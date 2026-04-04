#pragma once
#include "routes/AuthRoutes.hpp"
#include "routes/ChapterRoutes.hpp"
#include "routes/MemberRoutes.hpp"
#include "routes/MeetingRoutes.hpp"
#include "routes/EventRoutes.hpp"
#include "routes/AttendanceRoutes.hpp"
#include "routes/CalendarRoutes.hpp"
#include "routes/SettingsRoutes.hpp"
#include "routes/RoleRoutes.hpp"
#include "routes/PerkRoutes.hpp"
#include "routes/CheckinRoutes.hpp"
#include "routes/AuditRoutes.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "services/MemberService.hpp"
#include "services/MemberSyncService.hpp"
#include "services/MeetingService.hpp"
#include "services/EventService.hpp"
#include "services/ChapterService.hpp"
#include "services/AttendanceService.hpp"
#include "auth/AuthService.hpp"
#include "integrations/DiscordOAuth.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "integrations/GoogleCalendarClient.hpp"
#include "repositories/SettingsRepository.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "repositories/MeetingRepository.hpp"
#include "repositories/EventRepository.hpp"
#include "services/AuditService.hpp"

struct Services {
    ChapterService&         chapters;
    MemberService&          members;
    MeetingService&         meetings;
    EventService&           events;
    AttendanceService&      attendance;
    AuthService&            auth;
    DiscordOAuth&           oauth;
    DiscordClient&          discord;
    CalendarGenerator&      calendar;
    SettingsRepository&     settings;
    RoleMappingRepository&  role_mappings;
    ChapterMemberRepository& chapter_members;
    MemberSyncService&      member_sync;
    GoogleCalendarClient&   gcal;
    PerkLevelRepository&    perks;
    AttendanceRepository&   attendance_repo;
    MemberRepository&       member_repo;
    MeetingRepository&      meeting_repo;
    EventRepository&        event_repo;
    AuditService&           audit;
};

void register_all_routes(LugApp& app, Services& svc);
