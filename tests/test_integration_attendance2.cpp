#include "integration_test_base.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Attendance Routes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, AttendanceOverviewYearFilter) {
    // Create a meeting in 2025 and check in a member
    Meeting m;
    m.title = "Year Filter Mtg";
    m.start_time = "2025-06-01T19:00:00";
    m.end_time = "2025-06-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);
    attendance_svc->check_in(admin_member_id, "meeting", mtg.id, "", false);

    auto r = GET("/attendance/overview?year=2025", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");
}

TEST_F(IntegrationTest, AttendanceOverviewHideInactive) {
    auto r = GET("/attendance/overview?hide_inactive=1", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");
}

TEST_F(IntegrationTest, AttendanceOverviewSorting) {
    // Sort by meeting_count descending
    auto r = GET("/attendance/overview?sort=meeting_count&dir=desc", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");

    // Sort by display_name ascending
    auto r2 = GET("/attendance/overview?sort=display_name&dir=asc", admin_token);
    EXPECT_EQ(r2.code, 200);
    expect_contains(r2, "Attendance Overview");
}

TEST_F(IntegrationTest, AttendanceOverviewPagination) {
    auto r = GET("/attendance/overview?page=1", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");

    // Request page beyond what exists — should clamp to last page
    auto r2 = GET("/attendance/overview?page=9999", admin_token);
    EXPECT_EQ(r2.code, 200);
}

TEST_F(IntegrationTest, AttendanceMemberDetailView) {
    // Create a meeting and check in admin
    Meeting m;
    m.title = "Detail Mtg";
    m.start_time = "2026-03-15T19:00:00";
    m.end_time = "2026-03-15T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);
    attendance_svc->check_in(admin_member_id, "meeting", mtg.id, "", false);

    auto r = GET("/attendance/member/" + std::to_string(admin_member_id) + "/detail?year=2026", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Detail Mtg");
}

TEST_F(IntegrationTest, AttendanceMemberDetailNoAttendance) {
    // Request detail for a member with no attendance in the given year
    auto r = GET("/attendance/member/" + std::to_string(regular_member_id) + "/detail?year=2020", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "No attendance");
}

TEST_F(IntegrationTest, AttendanceMemberDetailPagination) {
    auto r = GET("/attendance/member/" + std::to_string(admin_member_id) + "/detail?year=2026&page=1", admin_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, AttendancePersonalPageMember) {
    // Regular member views their personal attendance page
    auto r = GET("/attendance", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "My Attendance");
}

TEST_F(IntegrationTest, AttendancePersonalPageWithHistory) {
    // Create a meeting and check in regular member, then view personal page
    Meeting m;
    m.title = "History Mtg";
    m.start_time = "2026-04-01T18:00:00";
    m.end_time = "2026-04-01T20:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);
    attendance_svc->check_in(regular_member_id, "meeting", mtg.id, "", false);

    auto r = GET("/attendance", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "My Attendance");
    expect_contains(r, "History Mtg");
}

TEST_F(IntegrationTest, AttendanceToggleVirtualAdmin) {
    Meeting m;
    m.title = "Toggle Virt Mtg";
    m.start_time = "2026-05-10T19:00:00";
    m.end_time = "2026-05-10T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    attendance_svc->check_in(regular_member_id, "meeting", mtg.id, "", false);
    auto attendees = attendance_repo->find_by_entity("meeting", mtg.id);
    ASSERT_EQ(attendees.size(), 1u);
    EXPECT_FALSE(attendees[0].is_virtual);

    // Toggle to virtual
    auto r = POST("/attendance/admin/" + std::to_string(attendees[0].id) + "/toggle-virtual",
        "entity_type=meeting&entity_id=" + std::to_string(mtg.id) + "&current=0",
        admin_token);
    EXPECT_EQ(r.code, 200);

    attendees = attendance_repo->find_by_entity("meeting", mtg.id);
    EXPECT_TRUE(attendees[0].is_virtual);

    // Toggle back to in-person
    auto r2 = POST("/attendance/admin/" + std::to_string(attendees[0].id) + "/toggle-virtual",
        "entity_type=meeting&entity_id=" + std::to_string(mtg.id) + "&current=1",
        admin_token);
    EXPECT_EQ(r2.code, 200);

    attendees = attendance_repo->find_by_entity("meeting", mtg.id);
    EXPECT_FALSE(attendees[0].is_virtual);
}

TEST_F(IntegrationTest, AttendanceToggleVirtualNonAdminForbidden) {
    Meeting m;
    m.title = "Forbidden Virt Mtg";
    m.start_time = "2026-05-11T19:00:00";
    m.end_time = "2026-05-11T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    attendance_svc->check_in(regular_member_id, "meeting", mtg.id, "", false);
    auto attendees = attendance_repo->find_by_entity("meeting", mtg.id);
    ASSERT_EQ(attendees.size(), 1u);

    auto r = POST("/attendance/admin/" + std::to_string(attendees[0].id) + "/toggle-virtual",
        "entity_type=meeting&entity_id=" + std::to_string(mtg.id) + "&current=0",
        member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, MeetingComplete) {
    Meeting m;
    m.title = "Complete Me";
    m.start_time = "2026-04-05T19:00:00";
    m.end_time = "2026-04-05T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);
    EXPECT_EQ(mtg.status, "scheduled");

    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/complete", "", admin_token);
    EXPECT_EQ(r.code, 200);

    auto completed = meeting_svc->get(mtg.id);
    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->status, "completed");
}

TEST_F(IntegrationTest, MeetingCompleteNotFound) {
    auto r = POST("/meetings/99999/complete", "", admin_token);
    EXPECT_EQ(r.code, 404);
}

TEST_F(IntegrationTest, MeetingPublishReportGraceful) {
    // No Discord forum configured — should still respond gracefully
    Meeting m;
    m.title = "Report Mtg";
    m.start_time = "2026-04-06T19:00:00";
    m.end_time = "2026-04-06T21:00:00";
    m.scope = "lug_wide";
    m.notes = "Some meeting notes here";
    auto mtg = meeting_svc->create(m);

    attendance_svc->check_in(admin_member_id, "meeting", mtg.id, "", false);

    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/publish-report", "", admin_token);
    // May succeed (200) or fail gracefully — should not be 500
    EXPECT_NE(r.code, 500);
    EXPECT_NE(r.code, 404);
}

TEST_F(IntegrationTest, EventPublishReportGraceful) {
    // No Discord forum configured — should still respond gracefully
    LugEvent ev;
    ev.title = "Report Event";
    ev.start_time = "2026-04-07T10:00:00";
    ev.end_time = "2026-04-07T18:00:00";
    ev.scope = "lug_wide";
    ev.status = "confirmed";
    ev.notes = "Some event notes here";
    auto created = event_svc->create(ev);

    attendance_svc->check_in(admin_member_id, "event", created.id, "", false);

    auto r = POST("/events/" + std::to_string(created.id) + "/publish-report", "", admin_token);
    // May succeed (200) or fail gracefully — should not be 500
    EXPECT_NE(r.code, 500);
    EXPECT_NE(r.code, 404);
}

TEST_F(IntegrationTest, EventPublishReportNotFound) {
    auto r = POST("/events/99999/publish-report", "", admin_token);
    EXPECT_EQ(r.code, 404);
}

TEST_F(IntegrationTest, AttendanceOverviewNonAdminForbidden) {
    auto r = GET("/attendance/overview", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, AttendanceOverviewHtmx) {
    auto r = GET_HTMX("/attendance/overview", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");
}

TEST_F(IntegrationTest, AttendancePersonalHtmx) {
    auto r = GET_HTMX("/attendance", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "My Attendance");
}
