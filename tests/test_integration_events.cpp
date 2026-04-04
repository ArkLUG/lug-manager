#include "integration_test_base.hpp"

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

