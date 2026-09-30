// Regression tests for access-control and injection fixes.
#include "integration_test_base.hpp"

// A chapter lead (or moderator) must not be able to change LUG roles via the
// JSON member endpoint - previously {"role":"admin"} on themselves succeeded.
TEST_F(IntegrationTest, ChapterLeadCannotSelfPromoteViaJsonPut) {
    auto r = PUT("/members/" + std::to_string(chapter_lead_member_id),
                 R"({"role":"admin"})", chapter_lead_token);
    EXPECT_EQ(r.code, 403);
    auto m = member_repo->find_by_id(chapter_lead_member_id);
    ASSERT_TRUE(m);
    EXPECT_EQ(m->role, "chapter_lead");
}

// Form edit by a non-admin must leave the target's role untouched (this used
// to let a chapter lead demote an admin by posting role=member).
TEST_F(IntegrationTest, ChapterLeadCannotChangeRoleViaForm) {
    auto r = POST("/members/" + std::to_string(regular_member_id),
                  "first_name=Reg&last_name=User&role=moderator", chapter_lead_token);
    auto m = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(m);
    EXPECT_EQ(m->role, "member");
}

// Permission to remove/toggle an attendance record must come from the entity
// the record belongs to, not from caller-supplied entity_type/entity_id.
TEST_F(IntegrationTest, AttendanceRemoveChecksRecordsOwnEntity) {
    Chapter ch;
    ch.name = "IDOR Chapter";
    ch.shorthand = "ID";
    auto chapter = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, chapter.id, "event_manager", admin_member_id);

    Meeting own;
    own.title = "Own Chapter Meeting";
    own.start_time = "2026-06-01T19:00:00";
    own.end_time = "2026-06-01T21:00:00";
    own.scope = "chapter";
    own.chapter_id = chapter.id;
    auto own_mtg = meeting_svc->create(own);

    Meeting other;
    other.title = "Someone Else's Meeting";
    other.start_time = "2026-06-02T19:00:00";
    other.end_time = "2026-06-02T21:00:00";
    other.scope = "lug_wide";
    auto other_mtg = meeting_svc->create(other);

    attendance_svc->check_in(admin_member_id, "meeting", other_mtg.id, "", false);
    auto rows = attendance_repo->find_by_entity("meeting", other_mtg.id);
    ASSERT_EQ(rows.size(), 1u);

    std::string spoofed = "entity_type=meeting&entity_id=" + std::to_string(own_mtg.id);
    auto t = POST("/attendance/admin/" + std::to_string(rows[0].id) + "/toggle-virtual",
                  spoofed + "&current=0", member_token);
    EXPECT_EQ(t.code, 403);
    auto r = POST("/attendance/admin/" + std::to_string(rows[0].id) + "/remove",
                  spoofed, member_token);
    EXPECT_EQ(r.code, 403);

    rows = attendance_repo->find_by_entity("meeting", other_mtg.id);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FALSE(rows[0].is_virtual);
}

// The overview search used to be spliced into SQL: an apostrophe broke it
// and a crafted value could rewrite the query.
TEST_F(IntegrationTest, AttendanceOverviewSearchIsParameterized) {
    Member m;
    m.first_name = "Zed";
    m.last_name = "O'Brien";
    m.display_name = "Zed O'Brien";
    member_repo->create(m);

    auto r = GET("/attendance/overview?search=Brien", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Zed");

    auto apostrophe = GET("/attendance/overview?search=O%27Brien", admin_token);
    EXPECT_EQ(apostrophe.code, 200);
    expect_contains(apostrophe, "Zed");

    // Classic tautology injection must match nothing rather than everything.
    auto inj = GET("/attendance/overview?search=nomatch%27%20OR%20%271%27%3D%271", admin_token);
    EXPECT_EQ(inj.code, 200);
    EXPECT_EQ(inj.body.find("Zed"), std::string::npos);
}

// check_in() must report duplicates as "not new" - it used to rely on
// last_insert_rowid(), which SQLite does not reset when INSERT OR IGNORE
// skips a row, so a repeat check-in right after any insert looked new.
TEST_F(IntegrationTest, DuplicateCheckinIsReportedAsNotNew) {
    Meeting m;
    m.title = "Dup Checkin Meeting";
    m.start_time = "2026-06-03T19:00:00";
    m.end_time = "2026-06-03T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    EXPECT_TRUE(attendance_repo->check_in(regular_member_id, "meeting", mtg.id, "", false));
    EXPECT_FALSE(attendance_repo->check_in(regular_member_id, "meeting", mtg.id, "", false));
    EXPECT_EQ(attendance_repo->count_by_entity("meeting", mtg.id), 1);

    EXPECT_FALSE(attendance_repo->set_virtual(999999, true));
}
