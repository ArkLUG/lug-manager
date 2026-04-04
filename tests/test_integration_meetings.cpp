#include "integration_test_base.hpp"

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

