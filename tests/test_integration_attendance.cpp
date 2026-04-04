#include "integration_test_base.hpp"

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

