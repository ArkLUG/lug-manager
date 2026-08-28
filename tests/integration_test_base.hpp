#pragma once
#include <gtest/gtest.h>
#include <crow.h>
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <vector>
#include <unistd.h>

#include "db/SqliteDatabase.hpp"
#include "db/Migrations.hpp"
#include "middleware/AuthMiddleware.hpp"
#include "auth/AuthService.hpp"
#include "auth/SessionStore.hpp"
#include "integrations/DiscordOAuth.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "integrations/GoogleCalendarClient.hpp"
#include "async/ThreadPool.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/MeetingRepository.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/ChapterRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/EventDayRepository.hpp"
#include "repositories/EventDayAttendanceRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "repositories/SettingsRepository.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "services/MemberService.hpp"
#include "services/MeetingService.hpp"
#include "services/EventService.hpp"
#include "services/ChapterService.hpp"
#include "services/AttendanceService.hpp"
#include "services/MemberSyncService.hpp"
#include "repositories/AuditLogRepository.hpp"
#include "repositories/ApiKeyRepository.hpp"
#include "repositories/PendingDiscordMatchRepository.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "services/AuditService.hpp"
#include "routes/Router.hpp"
#include "utils/Crypto.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Integration test fixture — boots a full Crow app on a random port
// ═══════════════════════════════════════════════════════════════════════════

class IntegrationTest : public ::testing::Test {
protected:
    std::unique_ptr<SqliteDatabase> db;
    std::unique_ptr<ThreadPool> pool;
    Config config;

    // Repos
    std::unique_ptr<MemberRepository> member_repo;
    std::unique_ptr<MeetingRepository> meeting_repo;
    std::unique_ptr<EventRepository> event_repo;
    std::unique_ptr<ChapterRepository> chapter_repo;
    std::unique_ptr<AttendanceRepository> attendance_repo;
    std::unique_ptr<EventDayRepository> event_day_repo;
    std::unique_ptr<EventDayAttendanceRepository> event_day_attendance_repo;
    std::unique_ptr<SettingsRepository> settings_repo;
    std::unique_ptr<RoleMappingRepository> role_mapping_repo;
    std::unique_ptr<ChapterMemberRepository> chapter_member_repo;
    std::unique_ptr<PerkLevelRepository> perk_level_repo;
    std::unique_ptr<AuditLogRepository> audit_log_repo;
    std::unique_ptr<ApiKeyRepository> api_key_repo;
    std::unique_ptr<PendingDiscordMatchRepository> pending_discord_match_repo;
    std::unique_ptr<SessionStore> session_store;

    // Integrations
    std::unique_ptr<DiscordOAuth> discord_oauth;
    std::unique_ptr<DiscordClient> discord_client;
    std::unique_ptr<CalendarGenerator> calendar;
    std::unique_ptr<GoogleCalendarClient> gcal_client;

    // Services
    std::unique_ptr<AuthService> auth_service;
    std::unique_ptr<MemberService> member_svc;
    std::unique_ptr<MeetingService> meeting_svc;
    std::unique_ptr<EventService> event_svc;
    std::unique_ptr<ChapterService> chapter_svc;
    std::unique_ptr<AttendanceService> attendance_svc;
    std::unique_ptr<MemberSyncService> member_sync_svc;
    std::unique_ptr<AuditService> audit_svc;

    // App
    std::unique_ptr<LugApp> app;
    std::thread server_thread;
    int port = 0;

    // Test session tokens
    std::string admin_token;
    std::string member_token;
    std::string chapter_lead_token;
    std::string event_manager_token;
    int64_t admin_member_id = 0;
    int64_t regular_member_id = 0;
    int64_t chapter_lead_member_id = 0;
    int64_t event_manager_member_id = 0;
    int64_t test_chapter_id = 0;

    // Creates an API key with the given scope via the repository directly (bypassing
    // the browser-only admin UI) and returns the raw plaintext key for test use.
    // All API key tests operate against the in-memory (":memory:") SQLite DB set up
    // in SetUp() below - no real/production data is ever touched.
    std::string make_api_key(const std::string& scope, const std::string& label = "test-key") {
        std::string raw_key  = generate_random_hex(32);
        std::string key_hash = sha256_hex(raw_key);
        api_key_repo->create(key_hash, label, scope, admin_member_id);
        return raw_key;
    }

    void SetUp() override {
        db = std::make_unique<SqliteDatabase>(":memory:");
        Migrations mig(*db);
        mig.run("sql/migrations");

        pool = std::make_unique<ThreadPool>(1);
        config.templates_dir = "src/templates";

        // Repos
        member_repo = std::make_unique<MemberRepository>(*db);
        meeting_repo = std::make_unique<MeetingRepository>(*db);
        event_repo = std::make_unique<EventRepository>(*db);
        chapter_repo = std::make_unique<ChapterRepository>(*db);
        attendance_repo = std::make_unique<AttendanceRepository>(*db);
        event_day_repo = std::make_unique<EventDayRepository>(*db);
        event_day_attendance_repo = std::make_unique<EventDayAttendanceRepository>(*db);
        settings_repo = std::make_unique<SettingsRepository>(*db);
        role_mapping_repo = std::make_unique<RoleMappingRepository>(*db);
        chapter_member_repo = std::make_unique<ChapterMemberRepository>(*db);
        perk_level_repo = std::make_unique<PerkLevelRepository>(*db);
        audit_log_repo = std::make_unique<AuditLogRepository>(*db);
        api_key_repo = std::make_unique<ApiKeyRepository>(*db);
        pending_discord_match_repo = std::make_unique<PendingDiscordMatchRepository>(*db);
        session_store = std::make_unique<SessionStore>(*db);

        // Integrations
        discord_oauth = std::make_unique<DiscordOAuth>(config);
        discord_client = std::make_unique<DiscordClient>(config, *pool);
        calendar = std::make_unique<CalendarGenerator>(*meeting_repo, *event_repo, config, chapter_repo.get());
        gcal_client = std::make_unique<GoogleCalendarClient>();

        // Services
        auth_service = std::make_unique<AuthService>(*session_store, *member_repo, *discord_oauth);
        member_svc = std::make_unique<MemberService>(*member_repo, discord_client.get());
        meeting_svc = std::make_unique<MeetingService>(*meeting_repo, *discord_client, *calendar, chapter_repo.get(), gcal_client.get());
        event_svc = std::make_unique<EventService>(*event_repo, *discord_client, *calendar, chapter_repo.get(), gcal_client.get(), event_day_repo.get());
        chapter_svc = std::make_unique<ChapterService>(*chapter_repo);
        attendance_svc = std::make_unique<AttendanceService>(*attendance_repo, *member_repo, *event_repo, *event_day_repo, *event_day_attendance_repo);
        member_sync_svc = std::make_unique<MemberSyncService>(*discord_client, *member_repo, *role_mapping_repo,
                                                                *chapter_repo, *chapter_member_repo,
                                                                *pending_discord_match_repo, settings_repo.get());
        audit_svc = std::make_unique<AuditService>(*audit_log_repo);

        // Create test members and sessions
        Member admin_m;
        admin_m.discord_user_id = "admin-test-001";
        admin_m.discord_username = "admin_user";
        admin_m.first_name = "Admin";
        admin_m.last_name = "User";
        admin_m.display_name = "Admin U.";
        admin_m.role = "admin";
        auto admin = member_repo->create(admin_m);
        admin_member_id = admin.id;
        admin_token = session_store->create(admin.id, "admin", admin.display_name);

        Member reg_m;
        reg_m.discord_user_id = "member-test-001";
        reg_m.discord_username = "regular_user";
        reg_m.first_name = "Regular";
        reg_m.last_name = "User";
        reg_m.display_name = "Regular U.";
        reg_m.role = "member";
        auto reg = member_repo->create(reg_m);
        regular_member_id = reg.id;
        member_token = session_store->create(reg.id, "member", reg.display_name);

        // Create chapter_lead user
        Member cl_m;
        cl_m.discord_user_id = "lead-test-001";
        cl_m.discord_username = "chapter_lead_user";
        cl_m.first_name = "Lead";
        cl_m.last_name = "User";
        cl_m.display_name = "Lead U.";
        cl_m.role = "chapter_lead";
        auto cl = member_repo->create(cl_m);
        chapter_lead_member_id = cl.id;
        chapter_lead_token = session_store->create(cl.id, "chapter_lead", cl.display_name);

        // Create event_manager user (global role is "member", chapter role is "event_manager")
        Member em_m;
        em_m.discord_user_id = "em-test-001";
        em_m.discord_username = "event_manager_user";
        em_m.first_name = "EventMgr";
        em_m.last_name = "User";
        em_m.display_name = "EventMgr U.";
        em_m.role = "member";
        auto em = member_repo->create(em_m);
        event_manager_member_id = em.id;
        event_manager_token = session_store->create(em.id, "member", em.display_name);

        // Create a test chapter and assign roles
        Chapter test_ch;
        test_ch.name = "Permission Test Chapter";
        test_ch.discord_announcement_channel_id = "";
        auto created_ch = chapter_repo->create(test_ch);
        test_chapter_id = created_ch.id;
        chapter_member_repo->upsert(chapter_lead_member_id, test_chapter_id, "lead", admin_member_id);
        chapter_member_repo->upsert(event_manager_member_id, test_chapter_id, "event_manager", admin_member_id);

        // Boot Crow app
        app = std::make_unique<LugApp>();
        app->get_middleware<AuthMiddleware>().auth_service = auth_service.get();
        app->get_middleware<ApiKeyMiddleware>().api_keys = api_key_repo.get();

        crow::mustache::set_global_base("src/templates");

        Services svc{
            *chapter_svc, *member_svc, *meeting_svc, *event_svc,
            *attendance_svc, *auth_service, *discord_oauth, *discord_client,
            *calendar, *settings_repo, *role_mapping_repo, *chapter_member_repo,
            *member_sync_svc, *gcal_client,
            *perk_level_repo, *attendance_repo, *member_repo,
            *meeting_repo, *event_repo,
            *event_day_repo, *event_day_attendance_repo,
            *audit_svc, *api_key_repo, *pending_discord_match_repo,
            config.discord_public_key
        };
        register_all_routes(*app, svc);

        // Pick a unique port using PID to avoid collisions between parallel test binaries.
        // Each test within a binary reuses the same port (sequential execution).
        port = 19000 + (static_cast<int>(getpid()) % 10000);
        app->port(port).concurrency(1);
        server_thread = std::thread([this]() { app->run(); });
        // Wait for server to be ready
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CURL* c = curl_easy_init();
            if (c) {
                std::string url = "http://127.0.0.1:" + std::to_string(port) + "/login";
                curl_easy_setopt(c, CURLOPT_URL, url.c_str());
                curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
                curl_easy_setopt(c, CURLOPT_TIMEOUT, 1L);
                CURLcode res = curl_easy_perform(c);
                curl_easy_cleanup(c);
                if (res == CURLE_OK) break;
            }
        }
    }

    void TearDown() override {
        app->stop();
        if (server_thread.joinable()) server_thread.join();
    }

    // HTTP helpers using curl
    struct Response {
        int code = 0;
        std::string body;
        std::string location; // redirect location
    };

    static size_t write_cb(void* data, size_t size, size_t nmemb, std::string* out) {
        out->append(static_cast<char*>(data), size * nmemb);
        return size * nmemb;
    }

    static size_t header_cb(char* data, size_t size, size_t nmemb, std::string* out) {
        out->append(data, size * nmemb);
        return size * nmemb;
    }

    Response http(const std::string& method, const std::string& path,
                  const std::string& body = "",
                  const std::string& session_token = "",
                  bool htmx = false,
                  const std::string& api_key = "",
                  bool json_body = false,
                  const std::vector<std::string>& extra_headers = {}) {
        CURL* curl = curl_easy_init();
        Response resp;
        std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
        std::string resp_body, resp_headers;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        struct curl_slist* headers = nullptr;
        if (!session_token.empty())
            headers = curl_slist_append(headers, ("Cookie: session=" + session_token).c_str());
        if (!api_key.empty())
            headers = curl_slist_append(headers, ("X-API-Key: " + api_key).c_str());
        if (htmx)
            headers = curl_slist_append(headers, "HX-Request: true");
        if (json_body)
            headers = curl_slist_append(headers, "Content-Type: application/json");
        else if (method == "POST" || method == "PUT" || method == "PATCH")
            headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
        for (const auto& h : extra_headers)
            headers = curl_slist_append(headers, h.c_str());
        if (headers)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        } else if (method == "PUT" || method == "PATCH" || method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
            if (!body.empty()) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        }

        curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.code);

        // Extract Location header (Crow may send lowercase)
        size_t loc_pos = resp_headers.find("Location: ");
        if (loc_pos == std::string::npos)
            loc_pos = resp_headers.find("location: ");
        if (loc_pos != std::string::npos) {
            size_t start = loc_pos + 10;
            size_t end = resp_headers.find("\r\n", start);
            resp.location = resp_headers.substr(start, end - start);
        }

        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        resp.body = resp_body;
        return resp;
    }

    // Shorthand helpers
    Response GET(const std::string& path, const std::string& token = "") {
        return http("GET", path, "", token);
    }
    Response GET_HTMX(const std::string& path, const std::string& token = "") {
        return http("GET", path, "", token, true);
    }
    Response POST(const std::string& path, const std::string& body = "", const std::string& token = "") {
        return http("POST", path, body, token);
    }
    Response POST_HTMX(const std::string& path, const std::string& body, const std::string& token = "") {
        return http("POST", path, body, token, true);
    }
    Response PUT(const std::string& path, const std::string& body = "", const std::string& token = "") {
        return http("PUT", path, body, token);
    }
    Response DEL(const std::string& path, const std::string& body = "", const std::string& token = "") {
        return http("DELETE", path, body, token);
    }

    // API-key-authenticated JSON helpers for /api/v1/* routes. api_key is sent via
    // X-API-Key; bodies are sent as application/json (empty body -> no body sent).
    Response API_GET(const std::string& path, const std::string& api_key) {
        return http("GET", path, "", "", false, api_key, true);
    }
    Response API_POST(const std::string& path, const std::string& json, const std::string& api_key) {
        return http("POST", path, json, "", false, api_key, true);
    }
    Response API_PUT(const std::string& path, const std::string& json, const std::string& api_key) {
        return http("PUT", path, json, "", false, api_key, true);
    }
    Response API_DELETE(const std::string& path, const std::string& api_key) {
        return http("DELETE", path, "", "", false, api_key, true);
    }
    Response PUT_JSON(const std::string& path, const std::string& body, const std::string& token = "") {
        CURL* curl = curl_easy_init();
        Response resp;
        std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
        std::string resp_body, resp_headers;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (!body.empty()) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        struct curl_slist* headers = nullptr;
        if (!token.empty())
            headers = curl_slist_append(headers, ("Cookie: session=" + token).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "HX-Request: true");
        if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.code);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        resp.body = resp_body;
        return resp;
    }

    // Content assertions
    void expect_contains(const Response& r, const std::string& text) {
        EXPECT_NE(r.body.find(text), std::string::npos)
            << "Expected body to contain: " << text << "\nBody (first 500 chars): " << r.body.substr(0, 500);
    }
    void expect_not_contains(const Response& r, const std::string& text) {
        EXPECT_EQ(r.body.find(text), std::string::npos)
            << "Expected body NOT to contain: " << text;
    }
};
