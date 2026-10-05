#pragma once
#include "chat/ChatHub.hpp"
#include "live/LiveRoutes.hpp"
#include "services/SiteSettings.hpp"
#include "routes/settings/SiteSettingsRoutes.hpp"
#include "routes/pages/HealthRoutes.hpp"
#include "routes/settings/ReminderSettingsRoutes.hpp"
#include "integrations/discord/DiscordProvider.hpp"
#include <string>
#include "routes/accounts/AuthRoutes.hpp"
#include "routes/members/ChapterRoutes.hpp"
#include "routes/members/MemberRoutes.hpp"
#include "routes/meetings/MeetingRoutes.hpp"
#include "routes/events/EventRoutes.hpp"
#include "routes/attendance/AttendanceRoutes.hpp"
#include "routes/pages/CalendarRoutes.hpp"
#include "routes/settings/SettingsRoutes.hpp"
#include "integrations/discord/sync/RoleRoutes.hpp"
#include "routes/members/PerkRoutes.hpp"
#include "routes/attendance/CheckinRoutes.hpp"
#include "routes/settings/AuditRoutes.hpp"
#include "routes/pages/HelpRoutes.hpp"
#include "routes/pages/ScheduleRoutes.hpp"
#include "routes/events/EventBlockRoutes.hpp"
#include "services/notifications/ReminderActions.hpp"
#include "routes/accounts/ApiKeyRoutes.hpp"
#include "integrations/discord/matches/DiscordMatchRoutes.hpp"
#include "integrations/discord/matches/DiscordInteractionsRoutes.hpp"
#include "routes/settings/BrandingRoutes.hpp"
#include "routes/api/members/MembersApiRoutes.hpp"
#include "routes/api/events/EventsApiRoutes.hpp"
#include "routes/api/events/MeetingsApiRoutes.hpp"
#include "routes/api/members/ChaptersApiRoutes.hpp"
#include "routes/api/members/ChapterMembersApiRoutes.hpp"
#include "routes/api/events/AttendanceApiRoutes.hpp"
#include "routes/api/members/PerkLevelsApiRoutes.hpp"
#include "routes/api/admin/RoleMappingsApiRoutes.hpp"
#include "routes/api/admin/AuditLogApiRoutes.hpp"
#include "routes/api/admin/SettingsApiRoutes.hpp"
#include "routes/api/admin/PendingDiscordMatchesApiRoutes.hpp"
#include "repositories/admin/ApiKeyRepository.hpp"
#include "integrations/discord/matches/PendingDiscordMatchRepository.hpp"
#include "repositories/members/PerkLevelRepository.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "repositories/events/EventDayRepository.hpp"
#include "repositories/events/EventDayAttendanceRepository.hpp"
#include "services/members/MemberService.hpp"
#include "integrations/discord/sync/MemberSyncService.hpp"
#include "services/events/MeetingService.hpp"
#include "services/events/EventService.hpp"
#include "services/members/ChapterService.hpp"
#include "services/events/AttendanceService.hpp"
#include "auth/AuthService.hpp"
#include "integrations/discord/DiscordOAuth.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "integrations/ical/CalendarGenerator.hpp"
#include "integrations/google_calendar/GoogleCalendarClient.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "integrations/discord/sync/RoleMappingRepository.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "repositories/events/MeetingRepository.hpp"
#include "repositories/events/EventRepository.hpp"
#include "services/AuditService.hpp"
#include "routes/events/RsvpRoutes.hpp"
#include "routes/events/DisplayRoutes.hpp"
#include "routes/members/DuesRoutes.hpp"
#include "routes/attendance/ExportRoutes.hpp"
#include "routes/members/MemberBulkRoutes.hpp"
#include "routes/settings/BackupRoutes.hpp"
#include "routes/accounts/SessionRoutes.hpp"
#include "routes/pages/PwaRoutes.hpp"
#include "routes/pages/StaticRoutes.hpp"
#include "routes/meetings/SeriesRoutes.hpp"
#include "routes/attendance/ReportRoutes.hpp"
#include "routes/events/ShiftRoutes.hpp"
#include "routes/community/GalleryRoutes.hpp"
#include "routes/accounts/AccountRoutes.hpp"
#include "routes/community/InventoryRoutes.hpp"
#include "routes/community/TreasuryRoutes.hpp"
#include "routes/members/MemberMergeRoutes.hpp"
#include "routes/events/ShowsRoutes.hpp"
#include "routes/community/FanCoLabRoutes.hpp"
#include "integrations/discord/time_repair/DiscordTimeRepairRoutes.hpp"
#include "routes/accounts/AccountSecurityRoutes.hpp"
#include "routes/settings/ChatSettingsRoutes.hpp"
#include "routes/settings/FeatureRoutes.hpp"
#include "routes/settings/SetupRoutes.hpp"
#include "services/notifications/Notifier.hpp"
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
    std::shared_ptr<ShiftRepository> shifts;
    std::shared_ptr<PhotoStore> photos;
    std::shared_ptr<Mailer> mailer;       // from LUG_SMTP_* when null
    std::shared_ptr<Notifier> notifier;
    std::shared_ptr<chat::ChatHub> chat;  // chat services (Discord, ...); made here when null
    std::shared_ptr<ReminderActions> reminder_actions;   // reminder DM buttons; made here
};

void register_all_routes(LugApp& app, Services& svc);
