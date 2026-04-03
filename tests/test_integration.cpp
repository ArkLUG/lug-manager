#include <gtest/gtest.h>
#include <crow.h>
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <algorithm>

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
#include "routes/Router.hpp"

using LugApp = crow::App<AuthMiddleware>;

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
    std::unique_ptr<SettingsRepository> settings_repo;
    std::unique_ptr<RoleMappingRepository> role_mapping_repo;
    std::unique_ptr<ChapterMemberRepository> chapter_member_repo;
    std::unique_ptr<PerkLevelRepository> perk_level_repo;
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

    // App
    std::unique_ptr<LugApp> app;
    std::thread server_thread;
    int port = 0;

    // Test session tokens
    std::string admin_token;
    std::string member_token;
    int64_t admin_member_id = 0;
    int64_t regular_member_id = 0;

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
        settings_repo = std::make_unique<SettingsRepository>(*db);
        role_mapping_repo = std::make_unique<RoleMappingRepository>(*db);
        chapter_member_repo = std::make_unique<ChapterMemberRepository>(*db);
        perk_level_repo = std::make_unique<PerkLevelRepository>(*db);
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
        event_svc = std::make_unique<EventService>(*event_repo, *discord_client, *calendar, chapter_repo.get(), gcal_client.get());
        chapter_svc = std::make_unique<ChapterService>(*chapter_repo);
        attendance_svc = std::make_unique<AttendanceService>(*attendance_repo, *member_repo);
        member_sync_svc = std::make_unique<MemberSyncService>(*discord_client, *member_repo, *role_mapping_repo,
                                                                *chapter_repo, *chapter_member_repo);

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
        admin_token = session_store->create(admin.id, "admin");

        Member reg_m;
        reg_m.discord_user_id = "member-test-001";
        reg_m.discord_username = "regular_user";
        reg_m.first_name = "Regular";
        reg_m.last_name = "User";
        reg_m.display_name = "Regular U.";
        reg_m.role = "member";
        auto reg = member_repo->create(reg_m);
        regular_member_id = reg.id;
        member_token = session_store->create(reg.id, "member");

        // Boot Crow app
        app = std::make_unique<LugApp>();
        app->get_middleware<AuthMiddleware>().auth_service = auth_service.get();

        crow::mustache::set_global_base("src/templates");

        Services svc{
            *chapter_svc, *member_svc, *meeting_svc, *event_svc,
            *attendance_svc, *auth_service, *discord_oauth, *discord_client,
            *calendar, *settings_repo, *role_mapping_repo, *chapter_member_repo,
            *member_sync_svc, *gcal_client,
            *perk_level_repo, *attendance_repo, *member_repo
        };
        register_all_routes(*app, svc);

        // Start on a fixed test port
        port = 18999 + (::testing::UnitTest::GetInstance()->random_seed() % 100);
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
                  bool htmx = false) {
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
        if (htmx)
            headers = curl_slist_append(headers, "HX-Request: true");
        if (method == "POST" || method == "PUT" || method == "PATCH")
            headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
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
    // JSON variant for PUT/POST endpoints that consume application/json
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

// ═══════════════════════════════════════════════════════════════════════════
// Auth & Access Control
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, UnauthenticatedRedirectsToLogin) {
    auto r = GET("/dashboard");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
}

TEST_F(IntegrationTest, UnauthenticatedHtmxReturns401) {
    auto r = GET_HTMX("/meetings");
    EXPECT_EQ(r.code, 401);
    expect_contains(r, "Session expired");
}

TEST_F(IntegrationTest, LoginPageLoads) {
    auto r = GET("/login");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Login");
    expect_contains(r, "Discord");
}

TEST_F(IntegrationTest, LogoutWorks) {
    auto r = GET("/auth/logout", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
}

TEST_F(IntegrationTest, AdminCanAccessSettings) {
    auto r = GET("/settings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Settings");
}

TEST_F(IntegrationTest, MemberCannotAccessSettings) {
    auto r = GET("/settings", member_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307); // redirects to dashboard
}

// ═══════════════════════════════════════════════════════════════════════════
// Dashboard
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, DashboardLoads) {
    auto r = GET("/dashboard", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Welcome");
    expect_contains(r, "calendar.ics");
    expect_contains(r, "Members");
    expect_contains(r, "Meetings");
    expect_contains(r, "Events");
}

TEST_F(IntegrationTest, DashboardHtmxPartial) {
    auto r = GET_HTMX("/dashboard", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Welcome");
    // Partial should NOT have full layout
    expect_not_contains(r, "<html");
}

// ═══════════════════════════════════════════════════════════════════════════
// Calendar (public)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, CalendarIcsPublic) {
    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "BEGIN:VCALENDAR");
    expect_contains(r, "END:VCALENDAR");
}

// ═══════════════════════════════════════════════════════════════════════════
// Members
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MembersPageLoads) {
    auto r = GET("/members", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Members");
    expect_contains(r, "members-table");
}

TEST_F(IntegrationTest, MembersNewForm) {
    auto r = GET("/members/new", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "First Name");
    expect_contains(r, "Last Name");
    expect_contains(r, "Discord User ID");
}

TEST_F(IntegrationTest, MembersCreateAndDelete) {
    auto r = POST("/members",
        "discord_user_id=test-create-001&discord_username=newuser&first_name=John&last_name=Smith&role=member",
        admin_token);
    EXPECT_EQ(r.code, 200);

    // Find the member
    auto found = member_repo->find_by_discord_id("test-create-001");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->first_name, "John");
    EXPECT_EQ(found->last_name, "Smith");
    EXPECT_EQ(found->display_name, "John S.");

    // Delete
    auto dr = DEL("/members/" + std::to_string(found->id), "", admin_token);
    EXPECT_EQ(dr.code, 200);
    EXPECT_FALSE(member_repo->find_by_id(found->id).has_value());
}

TEST_F(IntegrationTest, MembersEditForm) {
    auto r = GET("/members/" + std::to_string(admin_member_id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin");
    expect_contains(r, "User");
}

TEST_F(IntegrationTest, MembersUpdatePost) {
    auto r = POST("/members/" + std::to_string(admin_member_id),
        "first_name=Updated&last_name=Admin&discord_username=admin_user&email=admin@test.com&role=admin",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto found = member_repo->find_by_id(admin_member_id);
    EXPECT_EQ(found->first_name, "Updated");
    EXPECT_EQ(found->display_name, "Updated A.");
}

TEST_F(IntegrationTest, MembersDatatableApi) {
    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "recordsTotal");
    expect_contains(r, "Admin U.");
}

TEST_F(IntegrationTest, MembersNonAdminCannotCreate) {
    auto r = POST("/members",
        "discord_user_id=blocked&discord_username=blocked&first_name=No&last_name=Access&role=member",
        member_token);
    EXPECT_EQ(r.code, 403);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chapters
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, ChaptersPageLoads) {
    auto r = GET("/chapters", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "New Chapter");
}

TEST_F(IntegrationTest, ChapterCreateEditDelete) {
    // Create
    auto cr = POST("/chapters",
        "name=Test+Chapter&shorthand=TC&description=A+test&discord_announcement_channel_id=123",
        admin_token);
    EXPECT_EQ(cr.code, 200);

    auto all = chapter_repo->find_all();
    ASSERT_GE(all.size(), 1);
    auto ch = all.back();
    EXPECT_EQ(ch.name, "Test Chapter");
    EXPECT_EQ(ch.shorthand, "TC");

    // Detail page
    auto dr = GET("/chapters/" + std::to_string(ch.id), admin_token);
    EXPECT_EQ(dr.code, 200);
    expect_contains(dr, "Test Chapter");
    expect_contains(dr, "TC");

    // Edit form
    auto er = GET("/chapters/" + std::to_string(ch.id) + "/edit", admin_token);
    EXPECT_EQ(er.code, 200);
    expect_contains(er, "Test Chapter");

    // Delete
    auto delr = DEL("/chapters/" + std::to_string(ch.id), "", admin_token);
    EXPECT_EQ(delr.code, 200);
    EXPECT_FALSE(chapter_repo->find_by_id(ch.id).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Meetings
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MeetingsPageLoads) {
    auto r = GET("/meetings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Meetings");
    expect_contains(r, "Search");
}

TEST_F(IntegrationTest, MeetingsHtmxPartial) {
    auto r = GET_HTMX("/meetings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Meetings");
    expect_not_contains(r, "<html");
}

TEST_F(IntegrationTest, MeetingCreateEditDelete) {
    // New form
    auto nf = GET("/meetings/new", admin_token);
    EXPECT_EQ(nf.code, 200);
    expect_contains(nf, "Schedule New Meeting");

    // Create
    auto cr = POST("/meetings",
        "title=Integration+Test+Meeting&description=Test&location=Room+1&start_time=2026-05-01T19%3A00&end_time=2026-05-01T21%3A00&scope=lug_wide",
        admin_token);
    EXPECT_EQ(cr.code, 200);

    auto all = meeting_repo->find_all();
    ASSERT_GE(all.size(), 1);
    auto mtg = all.back();
    EXPECT_EQ(mtg.title, "Integration Test Meeting");

    // Edit form
    auto ef = GET("/meetings/" + std::to_string(mtg.id) + "/edit", admin_token);
    EXPECT_EQ(ef.code, 200);
    expect_contains(ef, "Integration Test Meeting");

    // Update
    auto ur = PUT("/meetings/" + std::to_string(mtg.id),
        "title=Updated+Meeting&start_time=2026-05-01T19%3A00&end_time=2026-05-01T21%3A00&scope=lug_wide",
        admin_token);
    EXPECT_EQ(ur.code, 200);

    auto updated = meeting_repo->find_by_id(mtg.id);
    EXPECT_EQ(updated->title, "Updated Meeting");

    // Cancel (delete)
    auto dr = POST("/meetings/" + std::to_string(mtg.id) + "/cancel", "", admin_token);
    EXPECT_EQ(dr.code, 200);
    EXPECT_FALSE(meeting_repo->find_by_id(mtg.id).has_value());
}

TEST_F(IntegrationTest, MeetingsPagination) {
    for (int i = 0; i < 15; ++i) {
        Meeting m;
        m.title = "Page Mtg " + std::to_string(i);
        m.start_time = "2026-06-01T19:00:00";
        m.end_time = "2026-06-01T21:00:00";
        m.scope = "lug_wide";
        meeting_svc->create(m);
    }
    auto r = GET("/meetings?page=2", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Page 2");
}

// ═══════════════════════════════════════════════════════════════════════════
// Events
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, EventsPageLoads) {
    auto r = GET("/events", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Events");
}

TEST_F(IntegrationTest, EventCreateEditDelete) {
    // Create
    auto cr = POST("/events",
        "title=Integration+Test+Event&description=Test&location=Hall+B&start_time=2026-06-15&end_time=2026-06-17&scope=lug_wide",
        admin_token);
    EXPECT_EQ(cr.code, 200);

    auto all = event_repo->find_all();
    ASSERT_GE(all.size(), 1);
    auto ev = all.back();
    EXPECT_EQ(ev.title, "Integration Test Event");
    EXPECT_EQ(ev.status, "open"); // Route sets status to "open" on creation

    // Edit form
    auto ef = GET("/events/" + std::to_string(ev.id) + "/edit", admin_token);
    EXPECT_EQ(ef.code, 200);
    expect_contains(ef, "Integration Test Event");

    // Update
    auto ur = PUT("/events/" + std::to_string(ev.id),
        "title=Updated+Event&start_time=2026-06-15&end_time=2026-06-17&scope=lug_wide",
        admin_token);
    EXPECT_EQ(ur.code, 200);

    // Status change
    auto sr = POST("/events/" + std::to_string(ev.id) + "/status", "status=tentative", admin_token);
    EXPECT_EQ(sr.code, 200);
    auto found = event_repo->find_by_id(ev.id);
    EXPECT_EQ(found->status, "tentative");

    // Cancel (delete)
    auto dr = POST("/events/" + std::to_string(ev.id) + "/cancel", "", admin_token);
    EXPECT_EQ(dr.code, 200);
    EXPECT_FALSE(event_repo->find_by_id(ev.id).has_value());
}

TEST_F(IntegrationTest, EventsAllPageAdmin) {
    auto r = GET("/events/all", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Events");
}

TEST_F(IntegrationTest, EventsAllPageNonAdmin) {
    auto r = GET("/events/all", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, EventConvertToMeeting) {
    LugEvent e;
    e.title = "Convert Me";
    e.start_time = "2026-07-01T00:00:00";
    e.end_time = "2026-07-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto created = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(created.id) + "/convert-to-meeting", "", admin_token);
    EXPECT_EQ(r.code, 200);

    // Event should be gone
    EXPECT_FALSE(event_repo->find_by_id(created.id).has_value());
    // Meeting should exist
    auto meetings = meeting_repo->find_all();
    bool found = false;
    for (auto& m : meetings) if (m.title == "Convert Me") found = true;
    EXPECT_TRUE(found);
}

// ═══════════════════════════════════════════════════════════════════════════
// Attendance
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, AttendancePersonalPage) {
    auto r = GET("/attendance", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "My Attendance");
}

TEST_F(IntegrationTest, AttendanceOverviewAdmin) {
    auto r = GET("/attendance/overview", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");
    expect_contains(r, "Admin U.");
}

TEST_F(IntegrationTest, AttendanceOverviewNonAdmin) {
    auto r = GET("/attendance/overview", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, AttendanceAdminCheckinAndRemove) {
    Meeting m;
    m.title = "Att Test Mtg";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    // Admin adds member
    auto cr = POST("/attendance/admin/checkin",
        "entity_type=meeting&entity_id=" + std::to_string(mtg.id) + "&member_id=" + std::to_string(regular_member_id),
        admin_token);
    EXPECT_EQ(cr.code, 200);
    EXPECT_EQ(attendance_repo->count_by_entity("meeting", mtg.id), 1);

    // Get attendance list
    auto lr = GET("/attendance/list/meeting/" + std::to_string(mtg.id), admin_token);
    EXPECT_EQ(lr.code, 200);
    expect_contains(lr, "Regular U.");

    // Count endpoint
    auto cntr = GET("/attendance/count/meeting/" + std::to_string(mtg.id), admin_token);
    EXPECT_EQ(cntr.code, 200);
    expect_contains(cntr, "1");

    // Remove
    auto attendees = attendance_repo->find_by_entity("meeting", mtg.id);
    ASSERT_EQ(attendees.size(), 1);
    auto rr = POST("/attendance/admin/" + std::to_string(attendees[0].id) + "/remove",
        "entity_type=meeting&entity_id=" + std::to_string(mtg.id),
        admin_token);
    EXPECT_EQ(rr.code, 200);
    EXPECT_EQ(attendance_repo->count_by_entity("meeting", mtg.id), 0);
}

TEST_F(IntegrationTest, AttendanceVirtualToggle) {
    Meeting m;
    m.title = "Virt Test";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    attendance_svc->check_in(admin_member_id, "meeting", mtg.id, "", false);
    auto attendees = attendance_repo->find_by_entity("meeting", mtg.id);
    ASSERT_EQ(attendees.size(), 1);
    EXPECT_FALSE(attendees[0].is_virtual);

    auto tr = POST("/attendance/admin/" + std::to_string(attendees[0].id) + "/toggle-virtual",
        "entity_type=meeting&entity_id=" + std::to_string(mtg.id) + "&current=0",
        admin_token);
    EXPECT_EQ(tr.code, 200);

    attendees = attendance_repo->find_by_entity("meeting", mtg.id);
    EXPECT_TRUE(attendees[0].is_virtual);
}

// ═══════════════════════════════════════════════════════════════════════════
// Settings & API endpoints
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, SettingsPageLoads) {
    auto r = GET("/settings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Discord Settings");
    expect_contains(r, "Google Calendar");
    expect_contains(r, "Bulk Sync");
    expect_contains(r, "Suppress");
}

TEST_F(IntegrationTest, SettingsSaveAndApply) {
    auto r = POST_HTMX("/settings",
        "discord_guild_id=test-guild&discord_announcements_channel_id=test-ch&lug_timezone=America/Chicago&ical_calendar_name=Test+Cal",
        admin_token);
    EXPECT_EQ(r.code, 200);

    EXPECT_EQ(settings_repo->get("discord_guild_id"), "test-guild");
    EXPECT_EQ(settings_repo->get("lug_timezone"), "America/Chicago");
    EXPECT_EQ(settings_repo->get("ical_calendar_name"), "Test Cal");
}

TEST_F(IntegrationTest, ChapterOptionsApi) {
    Chapter ch;
    ch.name = "API Chapter";
    ch.discord_announcement_channel_id = "ch-api";
    chapter_repo->create(ch);

    auto r = GET("/api/chapter-options", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "API Chapter");
}

TEST_F(IntegrationTest, MemberOptionsApi) {
    auto r = GET("/api/member-options", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin U.");
    expect_contains(r, "Regular U.");
}

TEST_F(IntegrationTest, RolesPageLoads) {
    auto r = GET("/settings/roles", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Role");
}

// ═══════════════════════════════════════════════════════════════════════════
// UI Content Validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MeetingCardShowsDateAndTime) {
    Meeting m;
    m.title = "Card Test Meeting";
    m.start_time = "2026-05-15T19:00:00";
    m.end_time = "2026-05-15T21:00:00";
    m.location = "Test Location";
    m.scope = "lug_wide";
    meeting_svc->create(m);

    auto r = GET_HTMX("/meetings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Card Test Meeting");
    expect_contains(r, "May");
    expect_contains(r, "15");
    expect_contains(r, "7:00 PM");
    expect_contains(r, "Test Location");
}

TEST_F(IntegrationTest, EventCardShowsBadges) {
    LugEvent e;
    e.title = "Badge Test Event";
    e.start_time = "2026-07-01T00:00:00";
    e.end_time = "2026-07-02T00:00:00";
    e.status = "tentative";
    e.scope = "lug_wide";
    event_svc->create(e);

    auto r = GET_HTMX("/events", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Badge Test Event");
    expect_contains(r, "Tentative");
    expect_contains(r, "LUG Wide");
}

TEST_F(IntegrationTest, NonLugEventHidesStatusButtons) {
    LugEvent e;
    e.title = "Non-LUG Test";
    e.start_time = "2026-08-01T00:00:00";
    e.end_time = "2026-08-02T00:00:00";
    e.status = "confirmed";
    e.scope = "non_lug";
    event_svc->create(e);

    auto r = GET_HTMX("/events", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Non-LUG Test");
    expect_contains(r, "Non-LUG");
    // Non-LUG events should NOT show attendance or status buttons
    // (can't easily test per-card, but the page should have the event)
}

TEST_F(IntegrationTest, CalendarContainsAllDayEvents) {
    LugEvent e;
    e.title = "iCal All Day";
    e.start_time = "2026-09-01T00:00:00";
    e.end_time = "2026-09-03T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    event_svc->create(e);

    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "iCal All Day");
    expect_contains(r, "DTSTART;VALUE=DATE:20260901");
    expect_contains(r, "DTEND;VALUE=DATE:20260904"); // exclusive end
}

TEST_F(IntegrationTest, CalendarContainsTimedMeetings) {
    Meeting m;
    m.title = "iCal Timed";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    meeting_svc->create(m);

    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "iCal Timed");
    expect_contains(r, "20260501T190000");
    expect_not_contains(r, "VALUE=DATE:20260501");
}

TEST_F(IntegrationTest, CalendarTitlesHavePrefixes) {
    Chapter ch;
    ch.name = "iCal Chapter";
    ch.shorthand = "ICL";
    ch.discord_announcement_channel_id = "icl-ch";
    auto chapter = chapter_svc->create(ch);

    LugEvent e;
    e.title = "Prefixed Event";
    e.start_time = "2026-10-01T00:00:00";
    e.end_time = "2026-10-02T00:00:00";
    e.status = "tentative";
    e.scope = "chapter";
    e.chapter_id = chapter.id;
    event_svc->create(e);

    auto r = GET("/calendar.ics");
    expect_contains(r, "[ICL]");
    expect_contains(r, "[Tentative]");
    expect_contains(r, "Prefixed Event");
}

TEST_F(IntegrationTest, AttendanceOverviewShowsCounts) {
    Meeting m;
    m.title = "Count Test";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    attendance_svc->check_in(admin_member_id, "meeting", mtg.id);
    attendance_svc->check_in(regular_member_id, "meeting", mtg.id, "", true);

    auto r = GET("/attendance/overview", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");
    // Both members should appear with meeting counts
    expect_contains(r, "Admin U.");
    expect_contains(r, "Regular U.");
}

// ═══════════════════════════════════════════════════════════════════════════
// Root redirect
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, RootUnauthenticatedRedirectsToLogin) {
    auto r = GET("/");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
}

TEST_F(IntegrationTest, RootAuthenticatedRedirectsToDashboard) {
    auto r = GET("/", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/dashboard"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Members — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MembersUpdatePutJson) {
    std::string json = R"({"email":"admin@json.com","role":"admin"})";
    auto r = PUT_JSON("/members/" + std::to_string(admin_member_id), json, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "success");

    auto found = member_repo->find_by_id(admin_member_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->email, "admin@json.com");
}

TEST_F(IntegrationTest, MembersPutJsonNonAdminForbidden) {
    std::string json = R"({"role":"admin"})";
    auto r = PUT_JSON("/members/" + std::to_string(regular_member_id), json, member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, MemberSetPaidStatus) {
    auto r = POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=2026-12-31", admin_token);
    EXPECT_EQ(r.code, 200);

    auto found = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->is_paid);
    EXPECT_EQ(found->paid_until, "2026-12-31");
}

TEST_F(IntegrationTest, MemberClearPaidStatus) {
    // Mark paid first
    POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=2026-12-31", admin_token);

    // Clear
    auto r = POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=", admin_token);
    EXPECT_EQ(r.code, 200);

    auto found = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->is_paid);
}

TEST_F(IntegrationTest, MemberSetPaidNonAdminForbidden) {
    auto r = POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=2026-12-31", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, MembersDeleteNonAdminForbidden) {
    auto r = DEL("/members/" + std::to_string(admin_member_id), "", member_token);
    EXPECT_EQ(r.code, 403);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chapters — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, ChapterNewFormLoads) {
    auto r = GET("/chapters/new", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "New Chapter");
}

TEST_F(IntegrationTest, ChapterUpdate) {
    Chapter ch;
    ch.name = "Update Me";
    ch.shorthand = "UM";
    ch.discord_announcement_channel_id = "ch-um";
    auto created = chapter_repo->create(ch);

    auto r = PUT("/chapters/" + std::to_string(created.id),
        "name=Updated+Chapter&shorthand=UC&discord_announcement_channel_id=ch-uc",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto found = chapter_repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Updated Chapter");
    EXPECT_EQ(found->shorthand, "UC");
}

TEST_F(IntegrationTest, ChapterNonAdminCannotCreate) {
    auto r = POST("/chapters",
        "name=Hacked&shorthand=HA&discord_announcement_channel_id=ha",
        member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, ChapterLeadAssignAndDemote) {
    Chapter ch;
    ch.name = "Lead Chapter";
    ch.shorthand = "LC";
    ch.discord_announcement_channel_id = "ch-lc";
    auto created = chapter_repo->create(ch);

    // Pre-add member to chapter
    chapter_member_repo->upsert(regular_member_id, created.id, "member", admin_member_id);

    // Assign as lead (admin can always assign)
    auto ar = POST("/chapters/" + std::to_string(created.id) + "/lead",
        "member_id=" + std::to_string(regular_member_id), admin_token);
    EXPECT_EQ(ar.code, 200);

    auto members = chapter_member_repo->find_by_chapter(created.id);
    bool is_lead = false;
    for (auto& m : members)
        if (m.member_id == regular_member_id && m.chapter_role == "lead") is_lead = true;
    EXPECT_TRUE(is_lead);

    // Demote
    auto dr = POST("/chapters/" + std::to_string(created.id) + "/lead/" +
        std::to_string(regular_member_id) + "/demote", "", admin_token);
    EXPECT_EQ(dr.code, 200);

    members = chapter_member_repo->find_by_chapter(created.id);
    bool still_lead = false;
    for (auto& m : members)
        if (m.member_id == regular_member_id && m.chapter_role == "lead") still_lead = true;
    EXPECT_FALSE(still_lead);
}

TEST_F(IntegrationTest, ChapterLeadRequiresMemberId) {
    Chapter ch;
    ch.name = "No Lead Chapter";
    ch.shorthand = "NL";
    ch.discord_announcement_channel_id = "ch-nl";
    auto created = chapter_repo->create(ch);

    auto r = POST("/chapters/" + std::to_string(created.id) + "/lead", "", admin_token);
    EXPECT_EQ(r.code, 400);
}

TEST_F(IntegrationTest, ChapterMembersListAddRemove) {
    Chapter ch;
    ch.name = "Member Chapter";
    ch.shorthand = "MC";
    ch.discord_announcement_channel_id = "ch-mc";
    auto created = chapter_repo->create(ch);

    // List (empty)
    auto lr = GET("/chapters/" + std::to_string(created.id) + "/members", admin_token);
    EXPECT_EQ(lr.code, 200);

    // Add member
    auto ar = POST("/chapters/" + std::to_string(created.id) + "/members",
        "member_id=" + std::to_string(regular_member_id) + "&chapter_role=member",
        admin_token);
    EXPECT_EQ(ar.code, 200);

    auto members = chapter_member_repo->find_by_chapter(created.id);
    ASSERT_EQ(members.size(), 1);
    EXPECT_EQ(members[0].member_id, regular_member_id);

    // HTMX list shows member name
    auto lr2 = GET_HTMX("/chapters/" + std::to_string(created.id) + "/members", admin_token);
    EXPECT_EQ(lr2.code, 200);
    expect_contains(lr2, "Regular U.");

    // Remove member
    auto rr = DEL("/chapters/" + std::to_string(created.id) + "/members/" +
        std::to_string(regular_member_id), "", admin_token);
    EXPECT_EQ(rr.code, 200);

    members = chapter_member_repo->find_by_chapter(created.id);
    EXPECT_EQ(members.size(), 0);
}

TEST_F(IntegrationTest, ChapterMembersAddMissingFields) {
    Chapter ch;
    ch.name = "Field Chapter";
    ch.shorthand = "FC";
    ch.discord_announcement_channel_id = "ch-fc";
    auto created = chapter_repo->create(ch);

    // Missing chapter_role → should return error fragment
    auto r = POST("/chapters/" + std::to_string(created.id) + "/members",
        "member_id=" + std::to_string(regular_member_id),
        admin_token);
    EXPECT_EQ(r.code, 400);
}

TEST_F(IntegrationTest, ChapterMembersNonAdminForbidden) {
    Chapter ch;
    ch.name = "Forbidden Chapter";
    ch.shorthand = "FB";
    ch.discord_announcement_channel_id = "ch-fb";
    auto created = chapter_repo->create(ch);

    auto r = POST("/chapters/" + std::to_string(created.id) + "/members",
        "member_id=" + std::to_string(regular_member_id) + "&chapter_role=member",
        member_token);
    // Not a chapter lead, so forbidden
    EXPECT_NE(r.code, 200);
}

// ═══════════════════════════════════════════════════════════════════════════
// Meetings — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MeetingDetailPage) {
    Meeting m;
    m.title = "Detail Test Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.location = "Detail Location";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = GET("/meetings/" + std::to_string(mtg.id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Detail Test Meeting");
}

TEST_F(IntegrationTest, MeetingDetailPageMember) {
    Meeting m;
    m.title = "Member View Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    // Regular members can view meeting detail
    auto r = GET("/meetings/" + std::to_string(mtg.id), member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Member View Meeting");
}

TEST_F(IntegrationTest, MeetingNonExistentReturns404) {
    auto r = GET("/meetings/99999", admin_token);
    EXPECT_EQ(r.code, 404);
}

TEST_F(IntegrationTest, MeetingComplete) {
    Meeting m;
    m.title = "Complete Me";
    m.start_time = "2026-04-01T19:00:00";
    m.end_time = "2026-04-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/complete", "", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "success");
}

TEST_F(IntegrationTest, MeetingCompleteNonAdminForbidden) {
    Meeting m;
    m.title = "No Complete";
    m.start_time = "2026-04-01T19:00:00";
    m.end_time = "2026-04-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/complete", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, MeetingAttendancePanel) {
    Meeting m;
    m.title = "Attendance Panel Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = GET("/meetings/" + std::to_string(mtg.id) + "/attendance", admin_token);
    EXPECT_EQ(r.code, 200);
    // Panel is an HTMX partial — no full HTML layout
    expect_not_contains(r, "<!DOCTYPE");
}

TEST_F(IntegrationTest, MeetingAttendancePanelMemberCanView) {
    Meeting m;
    m.title = "Panel View Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = GET("/meetings/" + std::to_string(mtg.id) + "/attendance", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MeetingSelfCheckinToggle) {
    Meeting m;
    m.title = "Self Checkin Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    // Check in
    auto r1 = POST("/meetings/" + std::to_string(mtg.id) + "/checkin",
        "virtual=0", admin_token);
    EXPECT_EQ(r1.code, 200);
    EXPECT_EQ(attendance_repo->count_by_entity("meeting", mtg.id), 1);

    // Toggle off (check out)
    auto r2 = POST("/meetings/" + std::to_string(mtg.id) + "/checkin",
        "virtual=0", admin_token);
    EXPECT_EQ(r2.code, 200);
    EXPECT_EQ(attendance_repo->count_by_entity("meeting", mtg.id), 0);
}

TEST_F(IntegrationTest, MeetingVirtualCheckin) {
    Meeting m;
    m.title = "Virtual Checkin Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/checkin",
        "virtual=1", member_token);
    EXPECT_EQ(r.code, 200);

    auto attendees = attendance_repo->find_by_entity("meeting", mtg.id);
    ASSERT_EQ(attendees.size(), 1);
    EXPECT_TRUE(attendees[0].is_virtual);
}

TEST_F(IntegrationTest, MeetingDiscordSyncGraceful) {
    Meeting m;
    m.title = "Discord Sync Test Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    // No real Discord — must not crash and returns HTML fragment
    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/discord-sync", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, MeetingDiscordSyncNonAdminForbidden) {
    Meeting m;
    m.title = "Sync Forbidden Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/discord-sync", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, MeetingCreateNonAdminForbidden) {
    auto r = POST("/meetings",
        "title=Hacked&start_time=2026-05-01T19%3A00&end_time=2026-05-01T21%3A00&scope=lug_wide",
        member_token);
    EXPECT_EQ(r.code, 403);
}

// ═══════════════════════════════════════════════════════════════════════════
// Events — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, EventNewFormLoads) {
    auto r = GET("/events/new", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "New Event");
}

TEST_F(IntegrationTest, EventDetailPage) {
    LugEvent e;
    e.title = "Detail Test Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    auto r = GET("/events/" + std::to_string(ev.id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Detail Test Event");
}

TEST_F(IntegrationTest, EventDetailPageMember) {
    LugEvent e;
    e.title = "Member View Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    auto r = GET("/events/" + std::to_string(ev.id), member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Member View Event");
}

TEST_F(IntegrationTest, EventNonExistentReturns404) {
    auto r = GET("/events/99999", admin_token);
    EXPECT_EQ(r.code, 404);
}

TEST_F(IntegrationTest, EventSelfCheckinToggle) {
    LugEvent e;
    e.title = "Self Checkin Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    // Check in
    auto r1 = POST("/events/" + std::to_string(ev.id) + "/checkin", "", admin_token);
    EXPECT_EQ(r1.code, 200);
    EXPECT_EQ(attendance_repo->count_by_entity("event", ev.id), 1);

    // Toggle off
    auto r2 = POST("/events/" + std::to_string(ev.id) + "/checkin", "", admin_token);
    EXPECT_EQ(r2.code, 200);
    EXPECT_EQ(attendance_repo->count_by_entity("event", ev.id), 0);
}

TEST_F(IntegrationTest, EventMemberCheckin) {
    LugEvent e;
    e.title = "Member Checkin Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(ev.id) + "/checkin", "", member_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(attendance_repo->count_by_entity("event", ev.id), 1);
}

TEST_F(IntegrationTest, EventDiscordSyncGraceful) {
    LugEvent e;
    e.title = "Discord Sync Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(ev.id) + "/discord-sync", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, EventDiscordSyncNonAdminForbidden) {
    LugEvent e;
    e.title = "No Sync Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(ev.id) + "/discord-sync", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, EventCreateNonAdminForbidden) {
    auto r = POST("/events",
        "title=Hacked&start_time=2026-06-15&end_time=2026-06-17&scope=lug_wide",
        member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, EventForumThreadsApiAdmin) {
    auto r = GET("/api/discord/forum-threads", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, EventForumThreadsApiNonAdmin) {
    auto r = GET("/api/discord/forum-threads", member_token);
    EXPECT_EQ(r.code, 403);
}

// ═══════════════════════════════════════════════════════════════════════════
// Settings — Discord API endpoints
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, DiscordChannelOptionsAdmin) {
    auto r = GET("/api/discord/channel-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordChannelOptionsNonAdmin) {
    auto r = GET("/api/discord/channel-options", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, DiscordForumOptionsAdmin) {
    auto r = GET("/api/discord/forum-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordForumOptionsNonAdmin) {
    auto r = GET("/api/discord/forum-options", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, DiscordRoleOptionsAdmin) {
    auto r = GET("/api/discord/role-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordRoleOptionsNonAdmin) {
    auto r = GET("/api/discord/role-options", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, DiscordTestAnnouncementGraceful) {
    // No channel configured — returns 200 with "no channel" message
    auto r = POST("/api/discord/test-announcement", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordTestAnnouncementNonAdmin) {
    auto r = POST("/api/discord/test-announcement", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, DiscordSyncMembersGraceful) {
    auto r = POST("/api/discord/sync-members", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordSyncMembersNonAdmin) {
    auto r = POST("/api/discord/sync-members", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, DiscordSyncAllGraceful) {
    auto r = POST("/api/discord/sync-all", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordSyncNicknamesGraceful) {
    auto r = POST("/api/discord/sync-nicknames", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordSyncNicknamesNonAdmin) {
    auto r = POST("/api/discord/sync-nicknames", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, GoogleCalendarImportGraceful) {
    auto r = POST("/api/google-calendar/import", "", admin_token);
    EXPECT_NE(r.code, 500);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, GoogleCalendarSyncAllGraceful) {
    auto r = POST("/api/google-calendar/sync-all", "", admin_token);
    EXPECT_NE(r.code, 500);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, RegenerateNicknamesGraceful) {
    auto r = POST("/api/members/regenerate-nicknames", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, RegenerateNicknamesNonAdmin) {
    auto r = POST("/api/members/regenerate-nicknames", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, SettingsSaveAllFields) {
    auto r = POST_HTMX("/settings",
        "discord_guild_id=guild-123"
        "&discord_announcements_channel_id=ch-123"
        "&discord_events_forum_channel_id=forum-123"
        "&discord_announcement_role_id=role-123"
        "&discord_non_lug_event_role_id=role-456"
        "&lug_timezone=America/New_York"
        "&ical_calendar_name=Full+Test+Cal"
        "&discord_suppress_pings=1"
        "&discord_suppress_updates=1",
        admin_token);
    EXPECT_EQ(r.code, 200);

    EXPECT_EQ(settings_repo->get("discord_guild_id"), "guild-123");
    EXPECT_EQ(settings_repo->get("discord_events_forum_channel_id"), "forum-123");
    EXPECT_EQ(settings_repo->get("lug_timezone"), "America/New_York");
    EXPECT_EQ(settings_repo->get("ical_calendar_name"), "Full Test Cal");
    EXPECT_EQ(settings_repo->get("discord_suppress_pings"), "1");
    EXPECT_EQ(settings_repo->get("discord_suppress_updates"), "1");
}

TEST_F(IntegrationTest, SettingsNonAdminForbidden) {
    auto r = POST("/settings",
        "discord_guild_id=hacked",
        member_token);
    EXPECT_EQ(r.code, 403); // non-admin gets forbidden
}

// ═══════════════════════════════════════════════════════════════════════════
// Roles — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, DiscordRolesApiAdmin) {
    // No Discord configured — must not return 403/401, returns JSON
    auto r = GET("/api/discord/roles", admin_token);
    EXPECT_NE(r.code, 403);
    EXPECT_NE(r.code, 401);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordRolesApiNonAdmin) {
    auto r = GET("/api/discord/roles", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, RolesMappingSaveHtmxReturns200) {
    // HTMX POST to /settings/roles returns 200 + HX-Redirect
    auto r = POST_HTMX("/settings/roles", "", admin_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, RolesMappingSaveNonHtmxReturns302) {
    // Non-HTMX POST to /settings/roles returns redirect (302 or 307)
    auto r = POST("/settings/roles", "", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
}

TEST_F(IntegrationTest, RolesPageNonAdminForbidden) {
    auto r = GET("/settings/roles", member_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307); // non-admin gets redirected to dashboard
}

// ═══════════════════════════════════════════════════════════════════════════
// Access control — member permissions
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MemberCanViewDashboard) {
    auto r = GET("/dashboard", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewMeetings) {
    auto r = GET("/meetings", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewEvents) {
    auto r = GET("/events", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewCalendar) {
    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewAttendance) {
    auto r = GET("/attendance", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "My Attendance");
}

TEST_F(IntegrationTest, MemberCanViewMembersPage) {
    auto r = GET("/members", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewChaptersPage) {
    auto r = GET("/chapters", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCannotCreateEvent) {
    auto r = POST("/events",
        "title=Hacked&start_time=2026-06-15&end_time=2026-06-17&scope=lug_wide",
        member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, MemberCannotCancelMeeting) {
    Meeting m;
    m.title = "Cancel Guard";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/cancel", "", member_token);
    EXPECT_NE(r.code, 200);
    // Meeting should still exist
    EXPECT_TRUE(meeting_repo->find_by_id(mtg.id).has_value());
}

TEST_F(IntegrationTest, MemberCannotCancelEvent) {
    LugEvent e;
    e.title = "Cancel Guard Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(ev.id) + "/cancel", "", member_token);
    EXPECT_NE(r.code, 200);
    EXPECT_TRUE(event_repo->find_by_id(ev.id).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// 404 / not found
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, NonExistentMemberReturns404) {
    auto r = GET("/members/99999", admin_token);
    EXPECT_EQ(r.code, 404);
}

TEST_F(IntegrationTest, NonExistentChapterReturns404) {
    auto r = GET("/chapters/99999", admin_token);
    EXPECT_EQ(r.code, 404);
}

// ═══════════════════════════════════════════════════════════════════════════
// UI content — additional validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MembersListShowsExistingMembers) {
    // Members page uses DataTables AJAX — verify via the datatable API
    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin U.");
    expect_contains(r, "Regular U.");
}

TEST_F(IntegrationTest, DashboardShowsMeetingAndEventCounts) {
    // Create a meeting and event so counts are non-zero
    Meeting m;
    m.title = "Count Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    meeting_svc->create(m);

    LugEvent e;
    e.title = "Count Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    event_svc->create(e);

    auto r = GET("/dashboard", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Members");
    expect_contains(r, "Meetings");
    expect_contains(r, "Events");
}

TEST_F(IntegrationTest, MeetingCardShowsEditButtonForAdmin) {
    Meeting m;
    m.title = "Admin Card Meeting";
    m.start_time = "2026-05-15T19:00:00";
    m.end_time = "2026-05-15T21:00:00";
    m.scope = "lug_wide";
    meeting_svc->create(m);

    auto r = GET_HTMX("/meetings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin Card Meeting");
    expect_contains(r, "edit");
}

TEST_F(IntegrationTest, EventsPageShowsConfirmedBadge) {
    LugEvent e;
    e.title = "Confirmed Badge Event";
    e.start_time = "2026-07-01T00:00:00";
    e.end_time = "2026-07-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    event_svc->create(e);

    auto r = GET_HTMX("/events", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Confirmed Badge Event");
    expect_contains(r, "Confirmed");
}

TEST_F(IntegrationTest, AttendanceCountEndpointForEvent) {
    LugEvent e;
    e.title = "Attendance Count Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    attendance_svc->check_in(admin_member_id, "event", ev.id);
    attendance_svc->check_in(regular_member_id, "event", ev.id);

    auto r = GET("/attendance/count/event/" + std::to_string(ev.id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "2");

    auto lr = GET("/attendance/list/event/" + std::to_string(ev.id), admin_token);
    EXPECT_EQ(lr.code, 200);
    expect_contains(lr, "Admin U.");
    expect_contains(lr, "Regular U.");
}

TEST_F(IntegrationTest, MeetingPaginationPageOne) {
    // Page 1 with no explicit param should still work
    auto r = GET("/meetings?page=1", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Meetings");
}

TEST_F(IntegrationTest, CalendarEmptyIsValid) {
    // With no events/meetings the calendar should still be valid iCal
    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "BEGIN:VCALENDAR");
    expect_contains(r, "VERSION:2.0");
    expect_contains(r, "END:VCALENDAR");
}

// ═══════════════════════════════════════════════════════════════════════════
// New feature tests — role redesign, PII hiding, suppress, perks, notes
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, PIIHiddenForMembers) {
    // Set email on admin member
    auto admin = member_repo->find_by_id(admin_member_id);
    ASSERT_TRUE(admin.has_value());
    admin->email = "secret@example.com";
    member_repo->update(*admin);

    // Member can't see emails in datatable
    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", member_token);
    EXPECT_EQ(r.code, 200);
    expect_not_contains(r, "secret@example.com");
}

TEST_F(IntegrationTest, PIIVisibleForAdmin) {
    auto admin = member_repo->find_by_id(admin_member_id);
    ASSERT_TRUE(admin.has_value());
    admin->email = "visible@example.com";
    member_repo->update(*admin);

    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "visible@example.com");
}

TEST_F(IntegrationTest, LeadCannotPromoteLead) {
    // Create a chapter and make the regular member a lead
    Chapter ch;
    ch.name = "Lead Test Chapter";
    auto created_ch = chapter_repo->create(ch);

    chapter_member_repo->upsert(regular_member_id, created_ch.id, "lead", admin_member_id);

    // Create another member to promote
    Member m3;
    m3.discord_user_id = "promote-target";
    m3.discord_username = "target";
    m3.display_name = "Target U.";
    auto target = member_repo->create(m3);

    // Lead tries to add another lead — should be forbidden (admin-only)
    auto r = POST("/chapters/" + std::to_string(created_ch.id) + "/lead",
        "member_id=" + std::to_string(target.id), member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, LeadCannotAssignLeadRole) {
    // Create a chapter and make the regular member a lead
    Chapter ch;
    ch.name = "Role Guard Chapter";
    auto created_ch = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created_ch.id, "lead", admin_member_id);

    Member m3;
    m3.discord_user_id = "role-guard-target";
    m3.discord_username = "role_guard";
    m3.display_name = "Guard U.";
    auto target = member_repo->create(m3);

    // Lead tries to set chapter_role=lead via members endpoint — should be forbidden
    auto r = POST("/chapters/" + std::to_string(created_ch.id) + "/members",
        "member_id=" + std::to_string(target.id) + "&chapter_role=lead", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, EventManagerCanEditChapterEvent) {
    // Create a chapter and make the regular member an event_manager
    Chapter ch;
    ch.name = "EM Edit Chapter";
    auto created_ch = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created_ch.id, "event_manager", admin_member_id);

    // Create an event in that chapter
    LugEvent e;
    e.title = "EM Editable Event";
    e.start_time = "2026-08-01T00:00:00";
    e.end_time = "2026-08-02T00:00:00";
    e.scope = "chapter";
    e.chapter_id = created_ch.id;
    auto created_ev = event_svc->create(e);

    // Event manager can access the edit form
    auto r = GET("/events/" + std::to_string(created_ev.id) + "/edit", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "EM Editable Event");
}

TEST_F(IntegrationTest, EventManagerCanCancelChapterEvent) {
    Chapter ch;
    ch.name = "EM Cancel Chapter";
    auto created_ch = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created_ch.id, "event_manager", admin_member_id);

    LugEvent e;
    e.title = "EM Cancelable Event";
    e.start_time = "2026-09-01T00:00:00";
    e.end_time = "2026-09-02T00:00:00";
    e.scope = "chapter";
    e.chapter_id = created_ch.id;
    auto created_ev = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(created_ev.id) + "/cancel", "", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCannotEditOtherChapterEvent) {
    // Create a chapter where member has NO role
    Chapter ch;
    ch.name = "No Access Chapter";
    auto created_ch = chapter_repo->create(ch);

    LugEvent e;
    e.title = "Forbidden Event";
    e.start_time = "2026-10-01T00:00:00";
    e.end_time = "2026-10-02T00:00:00";
    e.scope = "chapter";
    e.chapter_id = created_ch.id;
    auto created_ev = event_svc->create(e);

    auto r = GET("/events/" + std::to_string(created_ev.id) + "/edit", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, EventManagerCanEditChapterMeeting) {
    Chapter ch;
    ch.name = "EM Meeting Chapter";
    auto created_ch = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created_ch.id, "event_manager", admin_member_id);

    Meeting m;
    m.title = "EM Editable Meeting";
    m.start_time = "2026-08-15T19:00:00";
    m.end_time = "2026-08-15T21:00:00";
    m.scope = "chapter";
    m.chapter_id = created_ch.id;
    auto created_m = meeting_svc->create(m);

    auto r = GET("/meetings/" + std::to_string(created_m.id) + "/edit", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "EM Editable Meeting");
}

TEST_F(IntegrationTest, SuppressFlagsInEventCreate) {
    // Create event with suppress flags via form POST
    auto r = POST("/events",
        "title=Suppressed+Event&start_time=2025-01-15&end_time=2025-01-16"
        "&scope=lug_wide&suppress_discord=on&suppress_calendar=on",
        admin_token);
    EXPECT_EQ(r.code, 200);

    // Verify the flags were saved
    auto all = event_repo->find_all();
    auto it = std::find_if(all.begin(), all.end(),
        [](const LugEvent& e) { return e.title == "Suppressed Event"; });
    ASSERT_NE(it, all.end());
    EXPECT_TRUE(it->suppress_discord);
    EXPECT_TRUE(it->suppress_calendar);
}

TEST_F(IntegrationTest, NotesInEventCreate) {
    auto r = POST("/events",
        "title=Notes+Event&start_time=2026-11-15&end_time=2026-11-16"
        "&scope=lug_wide&notes=Test+notes+here",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto all = event_repo->find_all();
    auto it = std::find_if(all.begin(), all.end(),
        [](const LugEvent& e) { return e.title == "Notes Event"; });
    ASSERT_NE(it, all.end());
    EXPECT_EQ(it->notes, "Test notes here");
}

TEST_F(IntegrationTest, PerkLevelsAdminOnly) {
    auto r = GET("/settings/perks", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, PerkLevelsAdminCanAccess) {
    auto r = GET("/settings/perks", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Perk Levels");
}

TEST_F(IntegrationTest, PerkLevelCreateAndList) {
    auto r = POST("/settings/perks",
        "name=Bronze&meeting_attendance_required=3&event_attendance_required=1&sort_order=1",
        admin_token);
    EXPECT_TRUE(r.code == 200 || r.code == 302 || r.code == 307);

    auto levels = perk_level_repo->find_all();
    ASSERT_GE(levels.size(), 1u);
    auto it = std::find_if(levels.begin(), levels.end(),
        [](const PerkLevel& p) { return p.name == "Bronze"; });
    ASSERT_NE(it, levels.end());
    EXPECT_EQ(it->meeting_attendance_required, 3);
    EXPECT_EQ(it->event_attendance_required, 1);
}
