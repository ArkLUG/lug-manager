#include "routes/Router.hpp"

void register_all_routes(LugApp& app, Services& svc) {
    register_auth_routes(app, svc.auth, svc.oauth);
    register_chapter_routes(app, svc.chapters, svc.chapter_members, svc.members, svc.discord, svc.audit);
    register_member_routes(app, svc.members, svc.attendance_repo, svc.audit);
    register_meeting_routes(app, svc.meetings, svc.attendance, svc.chapter_members, svc.chapters, svc.discord, svc.audit);
    register_event_routes(app, svc.events, svc.attendance, svc.chapter_members, svc.discord, svc.members, svc.meetings, svc.chapters, svc.audit);
    register_attendance_routes(app, svc.attendance, svc.events, svc.meetings, svc.members, svc.chapter_members, svc.perks, svc.audit);
    register_calendar_routes(app, svc.calendar, svc.perks, svc.attendance_repo, svc.member_repo);
    register_settings_routes(app, svc.settings, svc.discord, svc.member_sync, svc.calendar, svc.gcal, svc.events, svc.meetings, svc.members, svc.audit, svc.pending_discord_matches);
    register_role_routes(app, svc.role_mappings, svc.chapters, svc.discord, svc.audit);
    register_perk_routes(app, svc.perks, svc.attendance_repo, svc.member_repo, svc.discord, svc.audit);
    register_checkin_routes(app, svc.meeting_repo, svc.event_repo,
                            svc.meetings, svc.events, svc.attendance, svc.members,
                            svc.member_repo, svc.chapter_members, svc.oauth, svc.audit);
    register_audit_routes(app, svc.audit);
    register_help_routes(app, svc.chapter_members);

    register_api_key_routes(app, svc.api_keys, svc.audit);
    register_discord_match_routes(app, svc.pending_discord_matches, svc.member_repo, svc.audit);
    register_discord_interactions_routes(app, svc.discord_public_key, svc.pending_discord_matches,
                                          svc.member_repo, svc.settings, svc.audit);
    register_members_api_routes(app, svc.members, svc.member_repo, svc.audit);
    register_events_api_routes(app, svc.events, svc.meetings, svc.event_day_repo,
                                svc.event_day_attendance_repo, svc.attendance_repo,
                                svc.chapters, svc.discord, svc.audit);
    register_meetings_api_routes(app, svc.meetings, svc.attendance_repo, svc.chapters, svc.discord, svc.audit);
    register_chapters_api_routes(app, svc.chapters, svc.audit);
    register_chapter_members_api_routes(app, svc.chapter_members, svc.chapters, svc.member_repo, svc.discord, svc.audit);
    register_attendance_api_routes(app, svc.attendance_repo, svc.event_day_attendance_repo, svc.audit);
    register_perk_levels_api_routes(app, svc.perks, svc.member_repo, svc.attendance_repo, svc.discord, svc.audit);
    register_role_mappings_api_routes(app, svc.role_mappings, svc.audit);
    register_audit_log_api_routes(app, svc.audit);
    register_settings_api_routes(app, svc.settings, svc.events, svc.meetings, svc.gcal, svc.audit);
    register_pending_discord_matches_api_routes(app, svc.pending_discord_matches, svc.member_repo, svc.audit);
}
