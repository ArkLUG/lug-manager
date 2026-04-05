#include "integration_test_base.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Audit Log — verify actions are logged correctly
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, AuditLogPageAdminOnly) {
    auto r = GET("/audit", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, AuditLogPageLoads) {
    auto r = GET("/audit", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Audit Log");
}

TEST_F(IntegrationTest, AuditMemberCreate) {
    auto r = POST("/members",
        "first_name=AuditTest&last_name=Member&discord_user_id=audit-create-001&discord_username=audittest&role=member",
        admin_token);
    EXPECT_EQ(r.code, 200);

    // Check audit log
    auto logs = audit_log_repo->find_paginated("AuditTest", "", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "member.create");
    EXPECT_NE(logs[0].entity_name.find("Audit"), std::string::npos);
    EXPECT_EQ(logs[0].actor_name, "Admin U.");
}

TEST_F(IntegrationTest, AuditMemberUpdate) {
    auto r = POST("/members/" + std::to_string(regular_member_id),
        "first_name=Updated&last_name=User&discord_username=regular_user&role=member",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "member.update", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "member.update");
    EXPECT_EQ(logs[0].entity_type, "member");
}

TEST_F(IntegrationTest, AuditMemberDelete) {
    // Create a member to delete
    Member m;
    m.discord_user_id = "audit-del-001";
    m.discord_username = "auditdel";
    m.display_name = "AuditDel U.";
    m.role = "member";
    auto created = member_repo->create(m);

    auto r = DEL("/members/" + std::to_string(created.id), "", admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "member.delete", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "member.delete");
    EXPECT_EQ(logs[0].entity_id, created.id);
}

TEST_F(IntegrationTest, AuditMemberDues) {
    auto r = POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=2027-03-31", admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "member.dues", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "member.dues");
    EXPECT_NE(logs[0].details.find("Marked paid"), std::string::npos);
}

TEST_F(IntegrationTest, AuditSelfEdit) {
    auto r = POST("/members/me",
        "first_name=Regular&last_name=User&sharing_email=all",
        member_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "member.self_edit", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "member.self_edit");
    EXPECT_EQ(logs[0].actor_name, "Regular U.");
}

TEST_F(IntegrationTest, AuditMeetingCreate) {
    auto r = POST("/meetings",
        "title=Audit+Meeting&location=Test+Loc&start_time=2026-07-01T19:00&end_time=2026-07-01T21:00&scope=lug_wide&suppress_discord=on",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "meeting.create", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "meeting.create");
    EXPECT_EQ(logs[0].entity_type, "meeting");
}

TEST_F(IntegrationTest, AuditMeetingDelete) {
    Meeting m;
    m.title = "Audit Del Meeting";
    m.start_time = "2026-07-10T19:00:00";
    m.end_time = "2026-07-10T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(created.id) + "/cancel", "", admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "meeting.delete", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "meeting.delete");
}

TEST_F(IntegrationTest, AuditEventCreate) {
    auto r = POST("/events",
        "title=Audit+Event&start_time=2026-08-01&end_time=2026-08-02&scope=lug_wide&suppress_discord=on",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "event.create", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "event.create");
    EXPECT_EQ(logs[0].entity_type, "event");
}

TEST_F(IntegrationTest, AuditEventStatusChange) {
    LugEvent ev;
    ev.title = "Audit Status Event";
    ev.start_time = "2026-08-15T00:00:00";
    ev.end_time = "2026-08-16T00:00:00";
    ev.status = "tentative";
    ev.scope = "lug_wide";
    auto created = event_svc->create(ev);

    auto r = POST("/events/" + std::to_string(created.id) + "/status",
        "status=confirmed", admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "event.status", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "event.status");
    EXPECT_NE(logs[0].details.find("confirmed"), std::string::npos);
}

TEST_F(IntegrationTest, AuditChapterCreate) {
    auto r = POST("/chapters",
        "name=Audit+Chapter&shorthand=AC&discord_announcement_channel_id=123",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "chapter.create", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "chapter.create");
    EXPECT_EQ(logs[0].entity_type, "chapter");
}

TEST_F(IntegrationTest, AuditChapterDelete) {
    Chapter ch;
    ch.name = "Audit Del Chapter";
    ch.discord_announcement_channel_id = "";
    auto created = chapter_repo->create(ch);

    auto r = DEL("/chapters/" + std::to_string(created.id), "", admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "chapter.delete", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "chapter.delete");
}

TEST_F(IntegrationTest, AuditAttendanceAdminCheckin) {
    Meeting m;
    m.title = "Audit Attendance Meeting";
    m.start_time = "2026-09-01T19:00:00";
    m.end_time = "2026-09-01T21:00:00";
    m.location = "Test";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    auto r = POST("/attendance/admin/checkin",
        "entity_type=meeting&entity_id=" + std::to_string(created.id) +
        "&member_id=" + std::to_string(regular_member_id),
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("", "attendance.checkin", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].action, "attendance.checkin");
}

TEST_F(IntegrationTest, AuditLogSearch) {
    // Create a member to generate a log entry with a known name
    POST("/members",
        "first_name=SearchAudit&last_name=Test&discord_user_id=audit-search-001&discord_username=auditsearch&role=member",
        admin_token);

    auto r = GET("/audit?search=SearchAudit", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "SearchAudit");
}

TEST_F(IntegrationTest, AuditLogFilterByAction) {
    // Ensure at least one member.create entry exists
    POST("/members",
        "first_name=FilterAudit&last_name=Test&discord_user_id=audit-filter-001&discord_username=auditfilter&role=member",
        admin_token);

    auto r = GET("/audit?action_filter=member", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "member.create");
}

TEST_F(IntegrationTest, AuditLogActorNameRecorded) {
    // Chapter lead performs an action
    auto r = POST("/members/me",
        "first_name=Lead&last_name=User",
        chapter_lead_token);
    EXPECT_EQ(r.code, 200);

    auto logs = audit_log_repo->find_paginated("Lead U.", "", 10, 0);
    ASSERT_GE(logs.size(), 1);
    EXPECT_EQ(logs[0].actor_name, "Lead U.");
}

TEST_F(IntegrationTest, AuditLogPagination) {
    // Create several entries
    for (int i = 0; i < 5; ++i) {
        POST("/members",
            "first_name=Page" + std::to_string(i) + "&last_name=Test&discord_user_id=audit-page-" +
            std::to_string(i) + "&discord_username=auditpage" + std::to_string(i) + "&role=member",
            admin_token);
    }

    auto r = GET("/audit?page=1", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Page 1");
}
