#include "routes/Router.hpp"

void register_all_routes(LugApp& app, Services& svc) {
    Features::bind(&svc.settings);
    if (!svc.mailer) svc.mailer = std::make_shared<Mailer>(Mailer::from_env());
    if (!svc.notifier)
        svc.notifier = std::make_shared<Notifier>(svc.attendance_repo.db(), svc.discord, svc.mailer, svc.public_url);
    register_auth_routes(app, svc.auth, svc.oauth, svc.public_url);
    register_email_auth_routes(app, svc.auth, svc.attendance_repo.db(), svc.notifier, svc.audit);
    register_chapter_routes(app, svc.chapters, svc.chapter_members, svc.members, svc.discord, svc.audit);
    register_member_routes(app, svc.members, svc.attendance_repo, svc.audit);
    register_meeting_routes(app, svc.meetings, svc.attendance, svc.chapter_members, svc.chapters, svc.discord, svc.audit);
    register_event_routes(app, svc.events, svc.attendance, svc.chapter_members, svc.discord, svc.members, svc.meetings, svc.chapters, svc.audit);
    if (!svc.rsvps) svc.rsvps = std::make_shared<RsvpRepository>(svc.attendance_repo.db());
    register_rsvp_routes(app, svc.events, svc.rsvps, svc.chapter_members, svc.audit, svc.notifier);
    if (!svc.displays) svc.displays = std::make_shared<DisplayRequestRepository>(svc.attendance_repo.db());
    register_display_routes(app, svc.events, svc.displays, svc.chapter_members, svc.audit);
    if (!svc.dues) svc.dues = std::make_shared<DuesRepository>(svc.attendance_repo.db());
    register_dues_routes(app, svc.members, svc.dues, svc.audit);
    register_member_bulk_routes(app, svc.members, svc.audit);
    register_session_routes(app, svc.auth, svc.audit);
    register_pwa_routes(app);
    register_static_routes(app);
    if (!svc.series) svc.series = std::make_shared<SeriesService>(svc.attendance_repo.db(), svc.meetings);
    register_report_routes(app, svc.attendance_repo.db(), svc.events, svc.event_day_repo,
                           svc.event_day_attendance_repo, svc.displays, svc.chapter_members, svc.audit);
    if (!svc.shifts) svc.shifts = std::make_shared<ShiftRepository>(svc.attendance_repo.db());
    register_shift_routes(app, svc.events, svc.shifts, svc.chapter_members, svc.audit);
    if (!svc.photos) svc.photos = std::make_shared<PhotoStore>(svc.data_dir);
    register_gallery_routes(app, svc.attendance_repo.db(), svc.photos, svc.events, svc.chapter_members,
                            svc.discord, svc.audit);
    register_account_routes(app, svc.attendance_repo.db(), svc.members, svc.photos, svc.audit);
    register_inventory_routes(app, svc.attendance_repo.db(), svc.audit, svc.photos, svc.events, svc.chapter_members);
    register_treasury_routes(app, svc.attendance_repo.db(), svc.audit,
                             std::make_shared<PhotoStore>(svc.data_dir, "uploads/receipts"));
    register_member_merge_routes(app, svc.attendance_repo.db(), svc.audit);
    register_shows_routes(app, svc.attendance_repo.db(), svc.settings, svc.audit);
    register_fan_colab_routes(app, svc.attendance_repo.db(), svc.settings, svc.photos, svc.audit);
    register_discord_time_repair_routes(app, svc.attendance_repo.db(), svc.discord, svc.audit);
    register_feature_routes(app, svc.settings, svc.audit);
    register_setup_routes(app, svc.attendance_repo.db(), svc.settings, svc.discord, svc.calendar, svc.audit, svc.public_url);
    register_series_routes(app, svc.series, svc.chapters, svc.chapter_members, svc.audit);
    if (!svc.backups) svc.backups = std::make_shared<BackupService>(svc.attendance_repo.db(), svc.data_dir);
    register_backup_routes(app, svc.backups, svc.settings, svc.audit);
    register_export_routes(app, svc.member_repo, svc.attendance_repo, svc.perks, svc.audit);
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
    register_discord_match_routes(app, svc.pending_discord_matches, svc.member_repo, svc.audit,
                                   svc.settings, svc.discord);
    register_branding_routes(app, svc.settings, svc.audit, svc.data_dir);
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
