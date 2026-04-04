#include "routes/Router.hpp"

void register_all_routes(LugApp& app, Services& svc) {
    register_auth_routes(app, svc.auth, svc.oauth);
    register_chapter_routes(app, svc.chapters, svc.chapter_members, svc.members, svc.discord);
    register_member_routes(app, svc.members, svc.attendance_repo);
    register_meeting_routes(app, svc.meetings, svc.attendance, svc.chapter_members, svc.chapters, svc.discord);
    register_event_routes(app, svc.events, svc.attendance, svc.chapter_members, svc.discord, svc.members, svc.meetings, svc.chapters);
    register_attendance_routes(app, svc.attendance, svc.events, svc.meetings, svc.chapter_members, svc.perks, svc.member_repo);
    register_calendar_routes(app, svc.calendar, svc.perks, svc.attendance_repo, svc.member_repo);
    register_settings_routes(app, svc.settings, svc.discord, svc.member_sync, svc.calendar, svc.gcal, svc.events, svc.meetings, svc.members);
    register_role_routes(app, svc.role_mappings, svc.chapters, svc.discord);
    register_perk_routes(app, svc.perks, svc.attendance_repo, svc.member_repo, svc.discord);
}
