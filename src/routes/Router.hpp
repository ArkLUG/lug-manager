#pragma once
#include <string>
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
#include "routes/HelpRoutes.hpp"
#include "routes/ApiKeyRoutes.hpp"
#include "routes/DiscordMatchRoutes.hpp"
#include "routes/DiscordInteractionsRoutes.hpp"
#include "routes/BrandingRoutes.hpp"
#include "routes/api/MembersApiRoutes.hpp"
#include "routes/api/EventsApiRoutes.hpp"
#include "routes/api/MeetingsApiRoutes.hpp"
#include "routes/api/ChaptersApiRoutes.hpp"
#include "routes/api/ChapterMembersApiRoutes.hpp"
#include "routes/api/AttendanceApiRoutes.hpp"
#include "routes/api/PerkLevelsApiRoutes.hpp"
#include "routes/api/RoleMappingsApiRoutes.hpp"
#include "routes/api/AuditLogApiRoutes.hpp"
#include "routes/api/SettingsApiRoutes.hpp"
#include "routes/api/PendingDiscordMatchesApiRoutes.hpp"
#include "repositories/ApiKeyRepository.hpp"
#include "repositories/PendingDiscordMatchRepository.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/EventDayRepository.hpp"
#include "repositories/EventDayAttendanceRepository.hpp"
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
#include "routes/RsvpRoutes.hpp"
#include "routes/DisplayRoutes.hpp"
#include "routes/DuesRoutes.hpp"
#include "routes/ExportRoutes.hpp"
#include "routes/MemberBulkRoutes.hpp"
#include "routes/BackupRoutes.hpp"
#include "routes/SessionRoutes.hpp"
#include "routes/PwaRoutes.hpp"
#include "routes/StaticRoutes.hpp"
#include "routes/SeriesRoutes.hpp"
#include "routes/ReportRoutes.hpp"
#include <memory>

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
    EventDayRepository&     event_day_repo;
    EventDayAttendanceRepository& event_day_attendance_repo;
    AuditService&           audit;
    ApiKeyRepository&       api_keys;
    PendingDiscordMatchRepository& pending_discord_matches;
    const std::string&      discord_public_key;
    const std::string&      data_dir; // directory for admin-uploaded files (logo, etc.) - same
                                       // durable volume the SQLite DB lives in, see main.cpp
    // Canonical external base URL (LUG_PUBLIC_URL). When set, OAuth/redirect
    // URLs are built from it instead of the request's Host/X-Forwarded-*
    // headers. Assigned after aggregate init (see main.cpp).
    std::string             public_url = "";
    // Created by register_all_routes() when null; shared_ptr because route
    // handlers hold it by value (Services itself may be a short-lived local).
    std::shared_ptr<RsvpRepository> rsvps;
    std::shared_ptr<DisplayRequestRepository> displays;
    std::shared_ptr<DuesRepository> dues;
    std::shared_ptr<BackupService> backups;
    std::shared_ptr<SeriesService> series;
};

void register_all_routes(LugApp& app, Services& svc);
