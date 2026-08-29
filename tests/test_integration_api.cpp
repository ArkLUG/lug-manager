// Integration tests for the /api/v1/* JSON CRUD API and its API-key auth/scope model.
//
// Uses the same IntegrationTest fixture as every other integration test in this suite:
// a real Crow app booted on a random localhost port, backed by an in-memory (":memory:")
// SQLite database created fresh in SetUp() for every test. No real/production data,
// files, or external services (Discord/Google Calendar) are ever touched - DiscordClient/
// GoogleCalendarClient are constructed with empty/test config, so any outbound calls they
// would make are no-ops or fail harmlessly in this environment. API keys are minted
// directly via ApiKeyRepository (make_api_key() helper in the fixture), not through the
// browser-only admin UI, since that requires a session cookie the API itself must not
// accept.
#include "integration_test_base.hpp"
#include <nlohmann/json.hpp>
#include <ctime>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────
// Auth / scope enforcement - the security-critical behavior
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiRejectsMissingKey) {
    auto r = GET("/api/v1/members");
    EXPECT_EQ(r.code, 401);
    expect_contains(r, "unauthenticated");
}

TEST_F(IntegrationTest, ApiRejectsUnknownKey) {
    auto r = API_GET("/api/v1/members", "0000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(r.code, 401);
}

TEST_F(IntegrationTest, ApiAcceptsBearerAuthorizationHeader) {
    std::string key = make_api_key("read");
    CURL* curl = curl_easy_init();
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/api/v1/members";
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + key).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    EXPECT_EQ(code, 200);
}

TEST_F(IntegrationTest, ApiSessionCookieAloneDoesNotGrantApiAccess) {
    // A valid admin session cookie, with no X-API-Key, must not authorize /api/v1/*.
    auto r = GET("/api/v1/members", admin_token);
    EXPECT_EQ(r.code, 401);
}

TEST_F(IntegrationTest, ApiKeyAloneDoesNotGrantSessionAdminUi) {
    // A valid API key, with no session cookie, must not authorize the session-only
    // /settings/api-keys admin UI (it should redirect/401 like any unauthenticated request).
    std::string key = make_api_key("admin");
    CURL* curl = curl_easy_init();
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/settings/api-keys";
    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-API-Key: " + key).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    EXPECT_NE(code, 200); // redirect (3xx) to /login, not a 200 admin page
}

TEST_F(IntegrationTest, ApiRevokedKeyIsRejected) {
    std::string raw_key  = generate_random_hex(32);
    std::string key_hash = sha256_hex(raw_key);
    auto created = api_key_repo->create(key_hash, "revoke-me", "read", admin_member_id);

    auto ok = API_GET("/api/v1/members", raw_key);
    EXPECT_EQ(ok.code, 200);

    api_key_repo->revoke(created.id);

    auto after = API_GET("/api/v1/members", raw_key);
    EXPECT_EQ(after.code, 401);
}

TEST_F(IntegrationTest, ApiReadScopeCannotWrite) {
    std::string key = make_api_key("read");
    auto r = API_POST("/api/v1/events",
        R"({"title":"Should Fail","start_time":"2030-01-01T10:00:00","end_time":"2030-01-01T12:00:00"})",
        key);
    EXPECT_EQ(r.code, 403);
    expect_contains(r, "forbidden");
}

TEST_F(IntegrationTest, ApiWriteScopeCannotAccessAdminOnlySettings) {
    std::string key = make_api_key("write");
    auto r = API_GET("/api/v1/settings/discord_guild_id", key);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, ApiWriteScopeCannotAccessAdminOnlyRoleMappings) {
    // role-mappings requires admin scope even for GET (privilege-sensitive config).
    std::string key = make_api_key("write");
    auto r = API_GET("/api/v1/role-mappings", key);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, ApiAdminScopeCanAccessSettings) {
    std::string key = make_api_key("admin");
    auto r = API_GET("/api/v1/settings/discord_guild_id", key);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, ApiWriteScopeCannotSetMemberRole) {
    std::string key = make_api_key("write");
    auto r = API_PUT("/api/v1/members/" + std::to_string(regular_member_id),
                      R"({"role":"admin"})", key);
    EXPECT_EQ(r.code, 403);

    // Confirm the role was NOT actually changed despite the attempt.
    auto m = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->role, "member");
}

TEST_F(IntegrationTest, ApiAdminScopeCanSetMemberRole) {
    std::string key = make_api_key("admin");
    auto r = API_PUT("/api/v1/members/" + std::to_string(regular_member_id),
                      R"({"role":"chapter_lead"})", key);
    EXPECT_EQ(r.code, 200);

    auto m = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->role, "chapter_lead");
}

TEST_F(IntegrationTest, ApiWriteScopeCannotDeleteMember) {
    std::string key = make_api_key("write");
    auto r = API_DELETE("/api/v1/members/" + std::to_string(regular_member_id), key);
    EXPECT_EQ(r.code, 403);
    EXPECT_TRUE(member_repo->find_by_id(regular_member_id).has_value());
}

TEST_F(IntegrationTest, ApiAdminScopeCanDeleteMember) {
    std::string key = make_api_key("admin");
    auto r = API_DELETE("/api/v1/members/" + std::to_string(regular_member_id), key);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(member_repo->find_by_id(regular_member_id).has_value());
}

// ─────────────────────────────────────────────────────────────────────────
// Members CRUD
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiMembersListAndGet) {
    std::string key = make_api_key("read");
    auto list = API_GET("/api/v1/members", key);
    EXPECT_EQ(list.code, 200);
    expect_contains(list, "\"data\"");
    expect_contains(list, "\"meta\"");

    auto one = API_GET("/api/v1/members/" + std::to_string(admin_member_id), key);
    EXPECT_EQ(one.code, 200);
    expect_contains(one, "admin_user");
}

TEST_F(IntegrationTest, ApiMembersGetMissingReturns404) {
    std::string key = make_api_key("read");
    auto r = API_GET("/api/v1/members/999999", key);
    EXPECT_EQ(r.code, 404);
}

TEST_F(IntegrationTest, ApiMembersCreateAndUpdate) {
    std::string key = make_api_key("write");
    auto created = API_POST("/api/v1/members",
        R"({"discord_user_id":"api-test-1","discord_username":"apiuser","first_name":"Api","last_name":"User"})",
        key);
    EXPECT_EQ(created.code, 201);
    auto body = json::parse(created.body);
    int64_t id = body["data"]["id"].get<int64_t>();
    EXPECT_GT(id, 0);

    auto updated = API_PUT("/api/v1/members/" + std::to_string(id),
                            R"({"city":"Fayetteville"})", key);
    EXPECT_EQ(updated.code, 200);
    auto m = member_repo->find_by_id(id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->city, "Fayetteville");
}

// Regression: PUT (and POST) /api/v1/members silently dropped is_paid and
// paid_until - every other field was read from the request body, but not
// these two, even though MemberService::update/create already apply them
// correctly once set on the struct. Found while backfilling paid dues data.
TEST_F(IntegrationTest, ApiMembersCreateAndUpdatePaidStatus) {
    std::string key = make_api_key("write");

    auto created = API_POST("/api/v1/members",
        R"({"first_name":"Paid","last_name":"OnCreate","is_paid":true,"paid_until":"2027-01-01"})",
        key);
    EXPECT_EQ(created.code, 201);
    auto created_body = json::parse(created.body);
    EXPECT_EQ(created_body["data"]["is_paid"].get<bool>(), true);
    EXPECT_EQ(created_body["data"]["paid_until"].get<std::string>(), "2027-01-01");
    int64_t id = created_body["data"]["id"].get<int64_t>();

    auto updated = API_PUT("/api/v1/members/" + std::to_string(id),
        R"({"is_paid":false})", key);
    EXPECT_EQ(updated.code, 200);
    auto updated_body = json::parse(updated.body);
    EXPECT_EQ(updated_body["data"]["is_paid"].get<bool>(), false);

    auto m = member_repo->find_by_id(id);
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->is_paid);
    EXPECT_EQ(m->paid_until, "2027-01-01"); // untouched by the is_paid-only PUT
}

// Regression: POST/PUT /api/v1/members read chapter_id from the request body
// into the Member struct, but that field is entirely derived (joined from
// chapter_members) - it has no column on the members table at all, so
// MemberService::update()/create() silently never persist it. The route
// handlers now call members.set_chapter(...) explicitly, same as the browser
// member-edit form already does. This test asserts on chapter_member_repo
// directly (not just the JSON response body) specifically because the
// response body alone can look correct while the DB write silently no-ops -
// that's exactly the failure mode being guarded against here.
TEST_F(IntegrationTest, ApiMembersSetChapterOnCreateAndUpdate) {
    std::string key = make_api_key("write");

    auto created = API_POST("/api/v1/members",
        R"({"first_name":"Chapter","last_name":"OnCreate","chapter_id":)" +
            std::to_string(test_chapter_id) + "}", key);
    EXPECT_EQ(created.code, 201);
    auto created_body = json::parse(created.body);
    EXPECT_EQ(created_body["data"]["chapter_id"].get<int64_t>(), test_chapter_id);
    int64_t id = created_body["data"]["id"].get<int64_t>();

    auto role = chapter_member_repo->get_chapter_role(id, test_chapter_id);
    ASSERT_TRUE(role.has_value());
    EXPECT_EQ(*role, "member");

    // Now move them to no chapter (0) via PUT.
    auto updated = API_PUT("/api/v1/members/" + std::to_string(id),
        R"({"chapter_id":0})", key);
    EXPECT_EQ(updated.code, 200);
    auto updated_body = json::parse(updated.body);
    EXPECT_EQ(updated_body["data"]["chapter_id"].get<int64_t>(), 0);
    EXPECT_FALSE(chapter_member_repo->get_chapter_role(id, test_chapter_id).has_value());
}

// ─────────────────────────────────────────────────────────────────────────
// Events CRUD (+ event days)
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiEventsCreateGetUpdateDelete) {
    std::string write_key = make_api_key("write");
    std::string admin_key = make_api_key("admin");

    auto created = API_POST("/api/v1/events",
        R"({"title":"API Event","start_time":"2030-05-01T10:00:00","end_time":"2030-05-03T10:00:00","scope":"lug_wide"})",
        write_key);
    EXPECT_EQ(created.code, 201);
    auto body = json::parse(created.body);
    int64_t id = body["data"]["id"].get<int64_t>();

    auto got = API_GET("/api/v1/events/" + std::to_string(id), write_key);
    EXPECT_EQ(got.code, 200);
    expect_contains(got, "API Event");

    auto days = API_GET("/api/v1/events/" + std::to_string(id) + "/days", write_key);
    EXPECT_EQ(days.code, 200);

    auto updated = API_PUT("/api/v1/events/" + std::to_string(id),
                            R"({"title":"API Event Updated"})", write_key);
    EXPECT_EQ(updated.code, 200);
    expect_contains(updated, "API Event Updated");

    // write scope cannot delete (admin-only, destructive)
    auto forbidden = API_DELETE("/api/v1/events/" + std::to_string(id), write_key);
    EXPECT_EQ(forbidden.code, 403);

    auto deleted = API_DELETE("/api/v1/events/" + std::to_string(id), admin_key);
    EXPECT_EQ(deleted.code, 200);
}

// Regression: PUT /api/v1/events silently dropped chapter_id (only POST read
// it), so re-scoping an event from non_lug/lug_wide to a specific chapter via
// the API had no effect.
TEST_F(IntegrationTest, ApiEventsUpdateChapterId) {
    std::string write_key = make_api_key("write");

    auto created = API_POST("/api/v1/events",
        R"({"title":"Rescope Event","start_time":"2030-05-01T10:00:00","end_time":"2030-05-01T10:00:00","scope":"non_lug"})",
        write_key);
    EXPECT_EQ(created.code, 201);
    auto body = json::parse(created.body);
    int64_t id = body["data"]["id"].get<int64_t>();

    auto updated = API_PUT("/api/v1/events/" + std::to_string(id),
        R"({"scope":"chapter","chapter_id":)" + std::to_string(test_chapter_id) + "}",
        write_key);
    EXPECT_EQ(updated.code, 200);
    auto updated_body = json::parse(updated.body);
    EXPECT_EQ(updated_body["data"]["chapter_id"].get<int64_t>(), test_chapter_id);
    EXPECT_EQ(updated_body["data"]["scope"].get<std::string>(), "chapter");
}

// New endpoint: POST /api/v1/events/<id>/checkin-token - generates (or returns
// the existing) QR check-in token, same as the browser's /events/<id>/generate-checkin.
TEST_F(IntegrationTest, ApiEventsCheckinTokenGenerateAndReuse) {
    std::string write_key = make_api_key("write");
    auto created = API_POST("/api/v1/events",
        R"({"title":"Checkin Token Event","start_time":"2030-05-01T10:00:00","end_time":"2030-05-01T12:00:00"})",
        write_key);
    int64_t id = json::parse(created.body)["data"]["id"].get<int64_t>();

    auto first = API_POST("/api/v1/events/" + std::to_string(id) + "/checkin-token", "", write_key);
    EXPECT_EQ(first.code, 200);
    auto first_body = json::parse(first.body);
    std::string token = first_body["data"]["checkin_token"].get<std::string>();
    EXPECT_FALSE(token.empty());

    // Calling again returns the SAME token, not a freshly generated one.
    auto second = API_POST("/api/v1/events/" + std::to_string(id) + "/checkin-token", "", write_key);
    EXPECT_EQ(second.code, 200);
    EXPECT_EQ(json::parse(second.body)["data"]["checkin_token"].get<std::string>(), token);
}

// New endpoint: POST /api/v1/events/<id>/publish-report - builds and posts a
// report to the configured Discord forum. No forum is configured in this test
// fixture, so publish_report_to_forum() no-ops (returns ""), but the endpoint
// itself must still succeed and not error just because Discord isn't wired up.
TEST_F(IntegrationTest, ApiEventsPublishReportSucceedsEvenWithoutDiscordConfigured) {
    std::string write_key = make_api_key("write");
    auto created = API_POST("/api/v1/events",
        R"({"title":"Report Event","start_time":"2030-05-01T10:00:00","end_time":"2030-05-01T12:00:00"})",
        write_key);
    int64_t id = json::parse(created.body)["data"]["id"].get<int64_t>();

    auto res = API_POST("/api/v1/events/" + std::to_string(id) + "/publish-report", "", write_key);
    EXPECT_EQ(res.code, 200);
}

// New endpoint: POST /api/v1/events/<id>/convert-to-meeting - creates a meeting
// from the event, copies attendance, deletes the event. Admin-only (destructive).
TEST_F(IntegrationTest, ApiEventsConvertToMeeting) {
    std::string write_key = make_api_key("write");
    std::string admin_key = make_api_key("admin");

    auto created = API_POST("/api/v1/events",
        R"({"title":"Convert Me","description":"desc","location":"HQ",)"
        R"("start_time":"2030-05-01T10:00:00","end_time":"2030-05-02T12:00:00","scope":"lug_wide"})",
        write_key);
    int64_t event_id = json::parse(created.body)["data"]["id"].get<int64_t>();

    auto days = json::parse(API_GET("/api/v1/events/" + std::to_string(event_id) + "/days", write_key).body);
    int64_t day_id = days["data"][0]["id"].get<int64_t>();
    API_POST("/api/v1/event-day-attendance",
        R"({"event_day_id":)" + std::to_string(day_id) +
        R"(,"member_id":)" + std::to_string(regular_member_id) + "}", write_key);

    // write scope cannot convert (destructive/admin-only)
    auto forbidden = API_POST("/api/v1/events/" + std::to_string(event_id) + "/convert-to-meeting", "", write_key);
    EXPECT_EQ(forbidden.code, 403);

    auto converted = API_POST("/api/v1/events/" + std::to_string(event_id) + "/convert-to-meeting", "", admin_key);
    EXPECT_EQ(converted.code, 200);
    int64_t meeting_id = json::parse(converted.body)["data"]["meeting_id"].get<int64_t>();
    EXPECT_GT(meeting_id, 0);

    // Original event is gone.
    auto gone = API_GET("/api/v1/events/" + std::to_string(event_id), write_key);
    EXPECT_EQ(gone.code, 404);

    // New meeting exists with the copied title, and attendance carried over.
    auto meeting = json::parse(API_GET("/api/v1/meetings/" + std::to_string(meeting_id), write_key).body);
    EXPECT_EQ(meeting["data"]["title"].get<std::string>(), "Convert Me");

    auto meeting_attendance = json::parse(
        API_GET("/api/v1/attendance?entity_type=meeting&entity_id=" + std::to_string(meeting_id), write_key).body);
    ASSERT_EQ(meeting_attendance["data"].size(), 1u);
    EXPECT_EQ(meeting_attendance["data"][0]["member_id"].get<int64_t>(), regular_member_id);
}

TEST_F(IntegrationTest, ApiEventsListPagination) {
    std::string key = make_api_key("write");
    for (int i = 0; i < 3; ++i) {
        API_POST("/api/v1/events",
            R"({"title":"Page Event )" + std::to_string(i) +
            R"(","start_time":"2030-06-0)" + std::to_string(i + 1) +
            R"(T10:00:00","end_time":"2030-06-0)" + std::to_string(i + 1) + R"(T12:00:00"})",
            key);
    }
    auto r = API_GET("/api/v1/events?limit=2&offset=0", key);
    EXPECT_EQ(r.code, 200);
    auto body = json::parse(r.body);
    EXPECT_LE(body["data"].size(), 2u);
    EXPECT_GE(body["meta"]["total"].get<int>(), 3);
}

// ─────────────────────────────────────────────────────────────────────────
// Meetings CRUD
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiMeetingsCreateGetUpdateDelete) {
    std::string write_key = make_api_key("write");
    std::string admin_key = make_api_key("admin");

    auto created = API_POST("/api/v1/meetings",
        R"({"title":"API Meeting","start_time":"2030-05-01T19:00:00","end_time":"2030-05-01T21:00:00"})",
        write_key);
    EXPECT_EQ(created.code, 201);
    auto body = json::parse(created.body);
    int64_t id = body["data"]["id"].get<int64_t>();

    auto got = API_GET("/api/v1/meetings/" + std::to_string(id), write_key);
    EXPECT_EQ(got.code, 200);

    auto updated = API_PUT("/api/v1/meetings/" + std::to_string(id),
                            R"({"location":"New Spot"})", write_key);
    EXPECT_EQ(updated.code, 200);

    EXPECT_EQ(API_DELETE("/api/v1/meetings/" + std::to_string(id), write_key).code, 403);
    EXPECT_EQ(API_DELETE("/api/v1/meetings/" + std::to_string(id), admin_key).code, 200);
}

// Regression: PUT /api/v1/meetings silently dropped chapter_id, same bug as
// the events endpoint above.
TEST_F(IntegrationTest, ApiMeetingsUpdateChapterId) {
    std::string write_key = make_api_key("write");

    auto created = API_POST("/api/v1/meetings",
        R"({"title":"Rescope Meeting","start_time":"2030-05-01T19:00:00","end_time":"2030-05-01T21:00:00","scope":"lug_wide"})",
        write_key);
    EXPECT_EQ(created.code, 201);
    auto body = json::parse(created.body);
    int64_t id = body["data"]["id"].get<int64_t>();

    auto updated = API_PUT("/api/v1/meetings/" + std::to_string(id),
        R"({"scope":"chapter","chapter_id":)" + std::to_string(test_chapter_id) + "}",
        write_key);
    EXPECT_EQ(updated.code, 200);
    auto updated_body = json::parse(updated.body);
    EXPECT_EQ(updated_body["data"]["chapter_id"].get<int64_t>(), test_chapter_id);
    EXPECT_EQ(updated_body["data"]["scope"].get<std::string>(), "chapter");
}

// New endpoint: POST /api/v1/meetings/<id>/checkin-token - same generate-or-reuse
// semantics as the events version, plus the virtual-meeting rejection.
TEST_F(IntegrationTest, ApiMeetingsCheckinTokenGenerateReuseAndVirtualRejected) {
    std::string write_key = make_api_key("write");

    auto created = API_POST("/api/v1/meetings",
        R"({"title":"Checkin Token Meeting","start_time":"2030-05-01T19:00:00","end_time":"2030-05-01T21:00:00"})",
        write_key);
    int64_t id = json::parse(created.body)["data"]["id"].get<int64_t>();

    auto first = API_POST("/api/v1/meetings/" + std::to_string(id) + "/checkin-token", "", write_key);
    EXPECT_EQ(first.code, 200);
    std::string token = json::parse(first.body)["data"]["checkin_token"].get<std::string>();
    EXPECT_FALSE(token.empty());

    auto second = API_POST("/api/v1/meetings/" + std::to_string(id) + "/checkin-token", "", write_key);
    EXPECT_EQ(json::parse(second.body)["data"]["checkin_token"].get<std::string>(), token);

    auto virtual_created = API_POST("/api/v1/meetings",
        R"({"title":"Virtual Meeting","start_time":"2030-05-01T19:00:00","end_time":"2030-05-01T21:00:00","is_virtual":true})",
        write_key);
    int64_t virtual_id = json::parse(virtual_created.body)["data"]["id"].get<int64_t>();
    auto virtual_res = API_POST("/api/v1/meetings/" + std::to_string(virtual_id) + "/checkin-token", "", write_key);
    EXPECT_EQ(virtual_res.code, 400);
}

// New endpoint: POST /api/v1/meetings/<id>/publish-report - mirrors the events
// version; no forum configured in this fixture, so it should still succeed
// (Discord publish is a no-op) rather than error.
TEST_F(IntegrationTest, ApiMeetingsPublishReportSucceedsEvenWithoutDiscordConfigured) {
    std::string write_key = make_api_key("write");
    auto created = API_POST("/api/v1/meetings",
        R"({"title":"Report Meeting","start_time":"2030-05-01T19:00:00","end_time":"2030-05-01T21:00:00"})",
        write_key);
    int64_t id = json::parse(created.body)["data"]["id"].get<int64_t>();

    auto res = API_POST("/api/v1/meetings/" + std::to_string(id) + "/publish-report", "", write_key);
    EXPECT_EQ(res.code, 200);
}

// ─────────────────────────────────────────────────────────────────────────
// Chapters CRUD
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiChaptersCreateGetUpdateDelete) {
    std::string write_key = make_api_key("write");
    std::string admin_key = make_api_key("admin");

    auto created = API_POST("/api/v1/chapters",
        R"({"name":"API Chapter","shorthand":"API","discord_announcement_channel_id":"123456"})", write_key);
    EXPECT_EQ(created.code, 201);
    auto body = json::parse(created.body);
    int64_t id = body["data"]["id"].get<int64_t>();

    auto list = API_GET("/api/v1/chapters", write_key);
    EXPECT_EQ(list.code, 200);
    expect_contains(list, "API Chapter");

    auto updated = API_PUT("/api/v1/chapters/" + std::to_string(id),
                            R"({"description":"Updated desc"})", write_key);
    EXPECT_EQ(updated.code, 200);

    EXPECT_EQ(API_DELETE("/api/v1/chapters/" + std::to_string(id), write_key).code, 403);
    EXPECT_EQ(API_DELETE("/api/v1/chapters/" + std::to_string(id), admin_key).code, 200);
}

// ─────────────────────────────────────────────────────────────────────────
// Chapter members (composite key)
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiChapterMembersUpsertAndRemove) {
    std::string write_key = make_api_key("write");
    std::string admin_key = make_api_key("admin");

    auto set_member = API_POST("/api/v1/chapter-members",
        R"({"member_id":)" + std::to_string(regular_member_id) +
        R"(,"chapter_id":)" + std::to_string(test_chapter_id) +
        R"(,"chapter_role":"member"})", write_key);
    EXPECT_EQ(set_member.code, 201);

    // write scope cannot grant "lead"
    auto set_lead_forbidden = API_POST("/api/v1/chapter-members",
        R"({"member_id":)" + std::to_string(regular_member_id) +
        R"(,"chapter_id":)" + std::to_string(test_chapter_id) +
        R"(,"chapter_role":"lead"})", write_key);
    EXPECT_EQ(set_lead_forbidden.code, 403);

    auto set_lead_ok = API_POST("/api/v1/chapter-members",
        R"({"member_id":)" + std::to_string(regular_member_id) +
        R"(,"chapter_id":)" + std::to_string(test_chapter_id) +
        R"(,"chapter_role":"lead"})", admin_key);
    EXPECT_EQ(set_lead_ok.code, 201);

    auto list = API_GET("/api/v1/chapter-members?chapter_id=" + std::to_string(test_chapter_id), write_key);
    EXPECT_EQ(list.code, 200);

    auto no_filter = API_GET("/api/v1/chapter-members", write_key);
    EXPECT_EQ(no_filter.code, 400);

    auto removed = API_DELETE("/api/v1/chapter-members?member_id=" + std::to_string(regular_member_id) +
                               "&chapter_id=" + std::to_string(test_chapter_id), admin_key);
    EXPECT_EQ(removed.code, 200);
}

// Regression: POST /api/v1/chapter-members previously had no DiscordClient
// dependency at all, so a lead promotion/demotion never synced the chapter's
// Discord lead role the way the browser's /chapters/<id>/lead route does.
// This test's fixture chapter has no discord_lead_role_id configured, so the
// sync path is a deliberate no-op here - it exercises that the lead <-> non-lead
// transition detection doesn't throw or misbehave when there's nothing to sync,
// and that the role change itself still persists correctly either way.
TEST_F(IntegrationTest, ApiChapterMembersLeadPromoteDemoteSyncsWithoutError) {
    std::string admin_key = make_api_key("admin");

    auto promote = API_POST("/api/v1/chapter-members",
        R"({"member_id":)" + std::to_string(regular_member_id) +
        R"(,"chapter_id":)" + std::to_string(test_chapter_id) +
        R"(,"chapter_role":"lead"})", admin_key);
    EXPECT_EQ(promote.code, 201);
    auto role_after_promote = chapter_member_repo->get_chapter_role(regular_member_id, test_chapter_id);
    ASSERT_TRUE(role_after_promote.has_value());
    EXPECT_EQ(*role_after_promote, "lead");

    auto demote = API_POST("/api/v1/chapter-members",
        R"({"member_id":)" + std::to_string(regular_member_id) +
        R"(,"chapter_id":)" + std::to_string(test_chapter_id) +
        R"(,"chapter_role":"member"})", admin_key);
    EXPECT_EQ(demote.code, 201);
    auto role_after_demote = chapter_member_repo->get_chapter_role(regular_member_id, test_chapter_id);
    ASSERT_TRUE(role_after_demote.has_value());
    EXPECT_EQ(*role_after_demote, "member");
}

// ─────────────────────────────────────────────────────────────────────────
// Attendance + event-day-attendance
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiAttendanceCheckInAndList) {
    std::string write_key = make_api_key("write");

    auto created = API_POST("/api/v1/meetings",
        R"({"title":"Attend Meeting","start_time":"2030-05-01T19:00:00","end_time":"2030-05-01T21:00:00"})",
        write_key);
    auto body = json::parse(created.body);
    int64_t meeting_id = body["data"]["id"].get<int64_t>();

    auto checkin = API_POST("/api/v1/attendance",
        R"({"member_id":)" + std::to_string(regular_member_id) +
        R"(,"entity_type":"meeting","entity_id":)" + std::to_string(meeting_id) + "}", write_key);
    EXPECT_EQ(checkin.code, 201);

    auto list = API_GET("/api/v1/attendance?entity_type=meeting&entity_id=" + std::to_string(meeting_id), write_key);
    EXPECT_EQ(list.code, 200);
    expect_contains(list, "Regular U.");

    auto by_member = API_GET("/api/v1/attendance/member/" + std::to_string(regular_member_id), write_key);
    EXPECT_EQ(by_member.code, 200);
}

TEST_F(IntegrationTest, ApiEventDayAttendanceCheckIn) {
    std::string write_key = make_api_key("write");

    auto created = API_POST("/api/v1/events",
        R"({"title":"Multi-day API Event","start_time":"2030-07-01T10:00:00","end_time":"2030-07-02T18:00:00"})",
        write_key);
    auto body = json::parse(created.body);
    int64_t event_id = body["data"]["id"].get<int64_t>();

    auto days = API_GET("/api/v1/events/" + std::to_string(event_id) + "/days", write_key);
    ASSERT_EQ(days.code, 200);
    auto days_body = json::parse(days.body);
    ASSERT_GE(days_body["data"].size(), 1u);
    int64_t day_id = days_body["data"][0]["id"].get<int64_t>();

    auto checkin = API_POST("/api/v1/event-day-attendance",
        R"({"event_day_id":)" + std::to_string(day_id) +
        R"(,"member_id":)" + std::to_string(regular_member_id) + "}", write_key);
    EXPECT_EQ(checkin.code, 201);

    auto by_day = API_GET("/api/v1/event-day-attendance?event_day_id=" + std::to_string(day_id), write_key);
    EXPECT_EQ(by_day.code, 200);

    auto by_event = API_GET("/api/v1/event-day-attendance/event/" + std::to_string(event_id), write_key);
    EXPECT_EQ(by_event.code, 200);
    expect_contains(by_event, "Regular U.");
}

// New endpoint: PUT /api/v1/attendance/<id> - update is_virtual on an existing flat
// attendance record. Previously there was no way to toggle this via the API at all
// (the browser has /attendance/admin/<id>/toggle-virtual with no equivalent).
TEST_F(IntegrationTest, ApiAttendanceUpdateIsVirtual) {
    std::string write_key = make_api_key("write");

    auto created = API_POST("/api/v1/meetings",
        R"({"title":"Virtual Toggle Meeting","start_time":"2030-05-01T19:00:00","end_time":"2030-05-01T21:00:00"})",
        write_key);
    int64_t meeting_id = json::parse(created.body)["data"]["id"].get<int64_t>();

    API_POST("/api/v1/attendance",
        R"({"member_id":)" + std::to_string(regular_member_id) +
        R"(,"entity_type":"meeting","entity_id":)" + std::to_string(meeting_id) + "}", write_key);

    auto list = API_GET("/api/v1/attendance?entity_type=meeting&entity_id=" + std::to_string(meeting_id), write_key);
    auto list_body = json::parse(list.body);
    ASSERT_EQ(list_body["data"].size(), 1u);
    int64_t attendance_id = list_body["data"][0]["id"].get<int64_t>();
    EXPECT_EQ(list_body["data"][0]["is_virtual"].get<bool>(), false);

    auto updated = API_PUT("/api/v1/attendance/" + std::to_string(attendance_id),
        R"({"is_virtual":true})", write_key);
    EXPECT_EQ(updated.code, 200);
    auto updated_body = json::parse(updated.body);
    EXPECT_EQ(updated_body["data"]["is_virtual"].get<bool>(), true);

    auto relist = API_GET("/api/v1/attendance?entity_type=meeting&entity_id=" + std::to_string(meeting_id), write_key);
    auto relist_body = json::parse(relist.body);
    EXPECT_EQ(relist_body["data"][0]["is_virtual"].get<bool>(), true);
}

TEST_F(IntegrationTest, ApiAttendanceUpdateIsVirtualMissingRecordReturns404) {
    std::string write_key = make_api_key("write");
    auto res = API_PUT("/api/v1/attendance/999999", R"({"is_virtual":true})", write_key);
    EXPECT_EQ(res.code, 404);
}

// ─────────────────────────────────────────────────────────────────────────
// Perk levels
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiPerkLevelsCreateGetUpdateDelete) {
    std::string write_key = make_api_key("write");
    std::string admin_key = make_api_key("admin");

    auto created = API_POST("/api/v1/perk-levels",
        R"({"name":"API Tier","year":2030,"meeting_attendance_required":2})", write_key);
    EXPECT_EQ(created.code, 201);
    auto body = json::parse(created.body);
    int64_t id = body["data"]["id"].get<int64_t>();

    auto by_year = API_GET("/api/v1/perk-levels?year=2030", write_key);
    EXPECT_EQ(by_year.code, 200);
    expect_contains(by_year, "API Tier");

    auto updated = API_PUT("/api/v1/perk-levels/" + std::to_string(id),
                            R"({"sort_order":5})", write_key);
    EXPECT_EQ(updated.code, 200);

    EXPECT_EQ(API_DELETE("/api/v1/perk-levels/" + std::to_string(id), write_key).code, 403);
    EXPECT_EQ(API_DELETE("/api/v1/perk-levels/" + std::to_string(id), admin_key).code, 200);
}

// New endpoint: POST /api/v1/perk-levels/clone - copies every tier from one
// year to another, refusing if the target year already has tiers.
TEST_F(IntegrationTest, ApiPerkLevelsClone) {
    std::string admin_key = make_api_key("admin");

    API_POST("/api/v1/perk-levels",
        R"({"name":"Bronze","year":2031,"meeting_attendance_required":1,"sort_order":1})", admin_key);
    API_POST("/api/v1/perk-levels",
        R"({"name":"Silver","year":2031,"meeting_attendance_required":3,"sort_order":2})", admin_key);

    auto cloned = API_POST("/api/v1/perk-levels/clone",
        R"({"source_year":2031,"target_year":2032})", admin_key);
    EXPECT_EQ(cloned.code, 200);
    EXPECT_EQ(json::parse(cloned.body)["data"]["cloned"].get<int>(), 2);

    auto target = API_GET("/api/v1/perk-levels?year=2032", admin_key);
    expect_contains(target, "Bronze");
    expect_contains(target, "Silver");

    // Cloning again into the same (now non-empty) target year is refused.
    auto conflict = API_POST("/api/v1/perk-levels/clone",
        R"({"source_year":2031,"target_year":2032})", admin_key);
    EXPECT_EQ(conflict.code, 400);
}

TEST_F(IntegrationTest, ApiPerkLevelsCloneRequiresAdmin) {
    std::string write_key = make_api_key("write");
    auto res = API_POST("/api/v1/perk-levels/clone",
        R"({"source_year":2031,"target_year":2032})", write_key);
    EXPECT_EQ(res.code, 403);
}

// New endpoint: POST /api/v1/perk-levels/sync-roles - bulk-syncs every
// member's Discord perk roles against the current year's tiers. No bot
// configured in this fixture, so add/remove_member_role calls throw and are
// caught (see the route's catch(...) around each call) - this test verifies
// the endpoint completes and reports a member count regardless.
TEST_F(IntegrationTest, ApiPerkLevelsSyncRoles) {
    std::string admin_key = make_api_key("admin");
    std::time_t now = std::time(nullptr);
    int current_year = std::localtime(&now)->tm_year + 1900;
    API_POST("/api/v1/perk-levels",
        R"({"name":"Current Year Tier","year":)" + std::to_string(current_year) +
        R"(,"meeting_attendance_required":0,"event_attendance_required":0,"discord_role_id":"999"})", admin_key);

    auto res = API_POST("/api/v1/perk-levels/sync-roles", "", admin_key);
    EXPECT_EQ(res.code, 200);
    auto body = json::parse(res.body);
    EXPECT_EQ(body["data"]["year"].get<int>(), current_year);
    EXPECT_GE(body["data"]["synced"].get<int>(), 1);
}

TEST_F(IntegrationTest, ApiPerkLevelsSyncRolesRequiresAdmin) {
    std::string write_key = make_api_key("write");
    auto res = API_POST("/api/v1/perk-levels/sync-roles", "", write_key);
    EXPECT_EQ(res.code, 403);
}

// ─────────────────────────────────────────────────────────────────────────
// Role mappings (admin-only for every verb, including GET)
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiRoleMappingsAdminOnlyCrud) {
    std::string admin_key = make_api_key("admin");

    auto created = API_POST("/api/v1/role-mappings",
        R"({"discord_role_id":"999","discord_role_name":"Testers","lug_role":"member"})", admin_key);
    EXPECT_EQ(created.code, 201);

    auto list = API_GET("/api/v1/role-mappings", admin_key);
    EXPECT_EQ(list.code, 200);
    expect_contains(list, "Testers");

    auto updated = API_PUT("/api/v1/role-mappings/999",
        R"({"discord_role_name":"Testers2","lug_role":"member"})", admin_key);
    EXPECT_EQ(updated.code, 200);

    auto removed = API_DELETE("/api/v1/role-mappings/999", admin_key);
    EXPECT_EQ(removed.code, 200);
}

// ─────────────────────────────────────────────────────────────────────────
// Audit log (read-only)
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiAuditLogIsReadOnly) {
    std::string write_key = make_api_key("write");
    std::string read_key  = make_api_key("read");

    // A mutation via the API itself should have produced at least one audit entry.
    auto trigger = API_POST("/api/v1/chapters",
        R"({"name":"Audit Trigger Chapter","discord_announcement_channel_id":"123456"})", write_key);
    ASSERT_EQ(trigger.code, 201);

    auto list = API_GET("/api/v1/audit-log", read_key);
    EXPECT_EQ(list.code, 200);
    expect_contains(list, "\"data\"");

    auto body = json::parse(list.body);
    ASSERT_GE(body["data"].size(), 1u);
    int64_t first_id = body["data"][0]["id"].get<int64_t>();

    auto one = API_GET("/api/v1/audit-log/" + std::to_string(first_id), read_key);
    EXPECT_EQ(one.code, 200);

    auto missing = API_GET("/api/v1/audit-log/999999", read_key);
    EXPECT_EQ(missing.code, 404);
}

// ─────────────────────────────────────────────────────────────────────────
// Settings (admin-only for every verb)
// ─────────────────────────────────────────────────────────────────────────

TEST_F(IntegrationTest, ApiSettingsAdminOnlyGetSet) {
    std::string admin_key = make_api_key("admin");
    std::string read_key  = make_api_key("read");

    EXPECT_EQ(API_GET("/api/v1/settings/discord_guild_id", read_key).code, 403);

    auto set = API_PUT("/api/v1/settings/discord_guild_id", R"({"value":"999888777"})", admin_key);
    EXPECT_EQ(set.code, 200);

    auto get = API_GET("/api/v1/settings/discord_guild_id", admin_key);
    EXPECT_EQ(get.code, 200);
    expect_contains(get, "999888777");
}
