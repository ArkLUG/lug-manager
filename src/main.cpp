#include <crow.h>
#include <crow/mustache.h>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

#include "config/Config.hpp"
#include "db/SqliteDatabase.hpp"
#include "db/Migrations.hpp"
#include "repositories/ChapterRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/MeetingRepository.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/SettingsRepository.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "services/ChapterService.hpp"
#include "services/MemberService.hpp"
#include "services/MeetingService.hpp"
#include "services/EventService.hpp"
#include "services/AttendanceService.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/DiscordOAuth.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "integrations/GoogleCalendarClient.hpp"
#include "async/ThreadPool.hpp"
#include "auth/AuthService.hpp"
#include "auth/SessionStore.hpp"
#include "middleware/AuthMiddleware.hpp"
#include "services/MemberSyncService.hpp"
#include "routes/Router.hpp"

int main() {
    try {
        // Load configuration from environment / config file
        Config config = load_config();

        // Set Crow mustache template base directory (use absolute path to avoid CWD issues)
        auto templates_abs = std::filesystem::absolute(config.templates_dir).string();
        crow::mustache::set_global_base(templates_abs);
        crow::mustache::set_base(templates_abs);
        std::cout << "[lug-manager] Templates (abs): " << templates_abs << "\n";

        // Open SQLite database
        SqliteDatabase db(config.db_path);

        // Run schema migrations
        Migrations migrations(db);
        migrations.run("sql/migrations");

        // Repositories (data access layer)
        ChapterRepository    chapter_repo(db);
        MemberRepository     member_repo(db);
        MeetingRepository    meeting_repo(db);
        EventRepository      event_repo(db);
        AttendanceRepository attendance_repo(db);
        SettingsRepository      settings_repo(db);
        RoleMappingRepository   role_mapping_repo(db);
        ChapterMemberRepository chapter_member_repo(db);
        PerkLevelRepository     perk_level_repo(db);

        // Async thread pool for non-blocking Discord API calls
        ThreadPool pool(4);

        // Integrations
        DiscordOAuth            discord_oauth(config);
        DiscordClient           discord_client(config, pool);
        CalendarGenerator       calendar(meeting_repo, event_repo, config, &chapter_repo);
        GoogleCalendarClient    gcal_client;

        // Seed settings from env on first run, then apply stored values
        {
            std::string guild            = settings_repo.get("discord_guild_id");
            std::string channel          = settings_repo.get("discord_announcements_channel_id");
            std::string forum_channel    = settings_repo.get("discord_events_forum_channel_id");
            std::string announce_role    = settings_repo.get("discord_announcement_role_id");
            std::string non_lug_role     = settings_repo.get("discord_non_lug_event_role_id");
            if (guild.empty() && !config.discord_guild_id.empty()) {
                settings_repo.set("discord_guild_id", config.discord_guild_id);
                guild = config.discord_guild_id;
            }
            if (channel.empty() && !config.discord_announcements_channel_id.empty()) {
                settings_repo.set("discord_announcements_channel_id",
                                   config.discord_announcements_channel_id);
                channel = config.discord_announcements_channel_id;
            }
            std::string timezone = settings_repo.get("lug_timezone");
            if (timezone.empty() && !config.ical_timezone.empty()) {
                settings_repo.set("lug_timezone", config.ical_timezone);
                timezone = config.ical_timezone;
            }
            if (timezone.empty()) timezone = "UTC";
            std::string cal_name = settings_repo.get("ical_calendar_name");
            if (cal_name.empty() && !config.ical_calendar_name.empty()) {
                settings_repo.set("ical_calendar_name", config.ical_calendar_name);
            }
            discord_client.reconfigure(guild, channel, forum_channel, announce_role, non_lug_role, timezone);
            discord_client.set_suppress_pings(settings_repo.get("discord_suppress_pings") == "1");
            discord_client.set_suppress_updates(settings_repo.get("discord_suppress_updates") == "1");
            calendar.set_timezone(timezone);

            // Google Calendar
            std::string gcal_sa_path = settings_repo.get("google_service_account_json_path");
            std::string gcal_cal_id  = settings_repo.get("google_calendar_id");
            if (!gcal_sa_path.empty() && !gcal_cal_id.empty())
                gcal_client.reconfigure(gcal_sa_path, gcal_cal_id, timezone);

            std::cout << "[lug-manager] Discord config:"
                      << " guild='" << guild << "'"
                      << " lug_channel='" << channel << "'"
                      << " forum_channel='" << forum_channel << "'"
                      << " announce_role='" << announce_role << "'"
                      << " non_lug_role='" << non_lug_role << "'"
                      << " timezone='" << timezone << "'\n";
        }

        // Auth layer
        SessionStore session_store(db);
        AuthService  auth_service(session_store, member_repo, discord_oauth,
                                  config.bootstrap_admin_discord_id,
                                  &discord_client, &role_mapping_repo);

        // Application services
        ChapterService    chapter_service(chapter_repo);
        MemberService     member_service(member_repo, &discord_client);
        MemberSyncService member_sync_service(discord_client, member_repo, role_mapping_repo,
                                              chapter_repo, chapter_member_repo);
        MeetingService    meeting_service(meeting_repo, discord_client, calendar, &chapter_repo, &gcal_client);
        EventService      event_service(event_repo, discord_client, calendar, &chapter_repo, &gcal_client);
        AttendanceService attendance_service(attendance_repo, member_repo);

        // Build the Crow app with AuthMiddleware
        LugApp app;
        app.get_middleware<AuthMiddleware>().auth_service = &auth_service;

        // Wire up all route handlers
        Services svc{
            chapter_service,
            member_service,
            meeting_service,
            event_service,
            attendance_service,
            auth_service,
            discord_oauth,
            discord_client,
            calendar,
            settings_repo,
            role_mapping_repo,
            chapter_member_repo,
            member_sync_service,
            gcal_client,
            perk_level_repo,
            attendance_repo,
            member_repo
        };
        register_all_routes(app, svc);

        // Background thread: purge expired sessions once per hour
        std::thread session_cleaner([&session_store] {
            while (true) {
                std::this_thread::sleep_for(std::chrono::hours(1));
                try {
                    session_store.purge_expired();
                } catch (const std::exception& e) {
                    std::cerr << "[session-cleaner] Error: " << e.what() << "\n";
                } catch (...) {
                    std::cerr << "[session-cleaner] Unknown error\n";
                }
            }
        });
        session_cleaner.detach();

        // Background thread: sync Discord guild members every 6 hours
        std::thread member_syncer([&member_sync_service] {
            // Initial delay: wait 30 seconds after startup before first sync
            std::this_thread::sleep_for(std::chrono::seconds(30));
            while (true) {
                try {
                    std::cout << "[member-sync] Starting periodic Discord member sync...\n";
                    SyncResult r = member_sync_service.sync_from_guild();
                    if (!r.error_message.empty()) {
                        std::cerr << "[member-sync] Error: " << r.error_message << "\n";
                    } else {
                        std::cout << "[member-sync] Done — imported=" << r.imported
                                  << " updated=" << r.updated
                                  << " skipped=" << r.skipped
                                  << " errors=" << r.errors << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[member-sync] Unexpected error: " << e.what() << "\n";
                } catch (...) {
                    std::cerr << "[member-sync] Unknown error\n";
                }
                std::this_thread::sleep_for(std::chrono::hours(6));
            }
        });
        member_syncer.detach();

        std::cout << "[lug-manager] Starting on port " << config.port << "\n";
        std::cout << "[lug-manager] Templates: " << config.templates_dir << "\n";
        std::cout << "[lug-manager] Calendar feed: /calendar.ics\n";

        app.port(config.port)
           .multithreaded()
           .run();

    } catch (const std::exception& e) {
        std::cerr << "[lug-manager] Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[lug-manager] Unknown fatal error\n";
        return 1;
    }

    return 0;
}
