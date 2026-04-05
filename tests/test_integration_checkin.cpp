#include "integration_test_base.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// QR Check-in — token generation, public page, search, check-in flows
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, GenerateCheckinTokenMeeting) {
    Meeting m;
    m.title = "Checkin Token Meeting";
    m.start_time = "2026-10-01T19:00:00";
    m.end_time = "2026-10-01T21:00:00";
    m.location = "Test Loc";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "checkin-qr");

    // Token should be stored
    auto updated = meeting_repo->find_by_id(created.id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_FALSE(updated->checkin_token.empty());
}

TEST_F(IntegrationTest, GenerateCheckinTokenEvent) {
    LugEvent ev;
    ev.title = "Checkin Token Event";
    ev.start_time = "2026-10-15T00:00:00";
    ev.end_time = "2026-10-16T00:00:00";
    ev.scope = "lug_wide";
    auto created = event_svc->create(ev);

    auto r = POST("/events/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "checkin-qr");
}

TEST_F(IntegrationTest, GenerateCheckinTokenReusesExisting) {
    Meeting m;
    m.title = "Reuse Token Meeting";
    m.start_time = "2026-10-02T19:00:00";
    m.end_time = "2026-10-02T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto first = meeting_repo->find_by_id(created.id);
    std::string token1 = first->checkin_token;

    POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto second = meeting_repo->find_by_id(created.id);
    EXPECT_EQ(token1, second->checkin_token); // same token reused
}

TEST_F(IntegrationTest, VirtualMeetingNoCheckin) {
    Meeting m;
    m.title = "Virtual No Checkin";
    m.start_time = "2026-10-03T19:00:00";
    m.end_time = "2026-10-03T21:00:00";
    m.location = "Virtual (Discord)";
    m.is_virtual = true;
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    EXPECT_EQ(r.code, 400);
}

TEST_F(IntegrationTest, CheckinPageLoads) {
    Meeting m;
    m.title = "Checkin Page Meeting";
    m.start_time = "2026-10-04T19:00:00";
    m.end_time = "2026-10-04T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    // Generate token
    POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto updated = meeting_repo->find_by_id(created.id);
    std::string token = updated->checkin_token;

    // Public page loads without auth
    auto r = GET("/checkin/" + token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Checkin Page Meeting");
    expect_contains(r, "Check In");
    expect_contains(r, "Discord");
    expect_contains(r, "Find Me");
    expect_contains(r, "New Member");
}

TEST_F(IntegrationTest, CheckinPageInvalidToken) {
    auto r = GET("/checkin/nonexistent-token-12345");
    EXPECT_EQ(r.code, 404);
    expect_contains(r, "Not Found");
}

TEST_F(IntegrationTest, CheckinSelectMember) {
    Meeting m;
    m.title = "Select Checkin Meeting";
    m.start_time = "2026-10-05T19:00:00";
    m.end_time = "2026-10-05T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto updated = meeting_repo->find_by_id(created.id);
    std::string token = updated->checkin_token;

    // Check in by selecting a member
    auto r = POST("/checkin/" + token + "/select",
        "member_id=" + std::to_string(regular_member_id));
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "checked in successfully");

    // Verify attendance
    EXPECT_TRUE(attendance_svc->is_checked_in(regular_member_id, "meeting", created.id));
}

TEST_F(IntegrationTest, CheckinSelectDuplicate) {
    Meeting m;
    m.title = "Dup Checkin Meeting";
    m.start_time = "2026-10-06T19:00:00";
    m.end_time = "2026-10-06T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto updated = meeting_repo->find_by_id(created.id);
    std::string token = updated->checkin_token;

    // Check in once
    POST("/checkin/" + token + "/select",
        "member_id=" + std::to_string(regular_member_id));

    // Try again — should get duplicate message
    auto r = POST("/checkin/" + token + "/select",
        "member_id=" + std::to_string(regular_member_id));
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "already checked in");
}

TEST_F(IntegrationTest, CheckinManualNewMember) {
    Meeting m;
    m.title = "Manual Checkin Meeting";
    m.start_time = "2026-10-07T19:00:00";
    m.end_time = "2026-10-07T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto updated = meeting_repo->find_by_id(created.id);
    std::string token = updated->checkin_token;

    // Manual entry for new member
    auto r = POST("/checkin/" + token + "/manual",
        "first_name=NewCheckin&last_name=Person&email=newcheckin@test.com");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "checked in");

    // Verify member was created
    auto all = member_repo->find_all();
    bool found = false;
    for (auto& mbr : all) {
        if (mbr.first_name == "NewCheckin" && mbr.last_name == "Person") {
            found = true;
            EXPECT_TRUE(attendance_svc->is_checked_in(mbr.id, "meeting", created.id));
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(IntegrationTest, CheckinManualExistingMember) {
    Meeting m;
    m.title = "Existing Manual Meeting";
    m.start_time = "2026-10-08T19:00:00";
    m.end_time = "2026-10-08T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto updated = meeting_repo->find_by_id(created.id);
    std::string token = updated->checkin_token;

    // Manual entry with name matching existing member "Regular User"
    auto r = POST("/checkin/" + token + "/manual",
        "first_name=Regular&last_name=User");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Welcome back");

    // Should have checked in the existing member, not created a new one
    EXPECT_TRUE(attendance_svc->is_checked_in(regular_member_id, "meeting", created.id));
}

TEST_F(IntegrationTest, CheckinSearchEndpoint) {
    Meeting m;
    m.title = "Search Checkin Meeting";
    m.start_time = "2026-10-09T19:00:00";
    m.end_time = "2026-10-09T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto updated = meeting_repo->find_by_id(created.id);
    std::string token = updated->checkin_token;

    // Search for "Admin"
    auto r = GET("/checkin/" + token + "/search?q=Admin");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin");
}

TEST_F(IntegrationTest, CheckinSearchTooShort) {
    auto r = GET("/checkin/sometoken/search?q=A");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "at least 2 characters");
}

TEST_F(IntegrationTest, CheckinMemberNonAdminForbidden) {
    Meeting m;
    m.title = "Forbidden Checkin";
    m.start_time = "2026-10-10T19:00:00";
    m.end_time = "2026-10-10T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    // Regular member cannot generate token
    auto r = POST("/meetings/" + std::to_string(created.id) + "/generate-checkin", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, CheckinEventFlow) {
    LugEvent ev;
    ev.title = "Checkin Event Flow";
    ev.start_time = "2026-11-01T00:00:00";
    ev.end_time = "2026-11-02T00:00:00";
    ev.scope = "lug_wide";
    auto created = event_svc->create(ev);

    POST("/events/" + std::to_string(created.id) + "/generate-checkin", "", admin_token);
    auto updated = event_repo->find_by_id(created.id);
    std::string token = updated->checkin_token;

    // Check in via select
    auto r = POST("/checkin/" + token + "/select",
        "member_id=" + std::to_string(admin_member_id));
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "checked in successfully");
    EXPECT_TRUE(attendance_svc->is_checked_in(admin_member_id, "event", created.id));
}
