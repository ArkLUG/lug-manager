#include "integration_test_base.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// ChapterRoutes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, ChapterDetailPageWithMembers) {
    // The test fixture already has chapter_lead and event_manager in test_chapter_id
    auto r = GET("/chapters/" + std::to_string(test_chapter_id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Permission Test Chapter");
    expect_contains(r, "Lead U.");
}

TEST_F(IntegrationTest, ChapterEditFormAdmin) {
    auto r = GET("/chapters/" + std::to_string(test_chapter_id) + "/edit", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Permission Test Chapter");
}

TEST_F(IntegrationTest, ChapterMemberAddEventManagerRole) {
    Chapter ch;
    ch.name = "EM Role Chapter";
    ch.shorthand = "EM";
    ch.discord_announcement_channel_id = "ch-em";
    auto created = chapter_repo->create(ch);

    auto r = POST("/chapters/" + std::to_string(created.id) + "/members",
        "member_id=" + std::to_string(regular_member_id) + "&chapter_role=event_manager",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto members = chapter_member_repo->find_by_chapter(created.id);
    ASSERT_EQ(members.size(), 1);
    EXPECT_EQ(members[0].chapter_role, "event_manager");
}

TEST_F(IntegrationTest, ChapterMemberRemoveByAdmin) {
    Chapter ch;
    ch.name = "Remove Chapter";
    ch.shorthand = "RC";
    ch.discord_announcement_channel_id = "ch-rc";
    auto created = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created.id, "member", admin_member_id);

    auto r = DEL("/chapters/" + std::to_string(created.id) + "/members/" +
        std::to_string(regular_member_id), "", admin_token);
    EXPECT_EQ(r.code, 200);

    auto members = chapter_member_repo->find_by_chapter(created.id);
    EXPECT_EQ(members.size(), 0);
}

TEST_F(IntegrationTest, ChapterDeleteNonAdminForbidden) {
    Chapter ch;
    ch.name = "No Delete";
    ch.shorthand = "ND";
    ch.discord_announcement_channel_id = "ch-nd";
    auto created = chapter_repo->create(ch);

    auto r = DEL("/chapters/" + std::to_string(created.id), "", member_token);
    EXPECT_EQ(r.code, 403);
    // Chapter should still exist
    EXPECT_TRUE(chapter_repo->find_by_id(created.id).has_value());
}

TEST_F(IntegrationTest, ChapterMemberListPageLoads) {
    auto r = GET("/chapters/" + std::to_string(test_chapter_id) + "/members", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Permission Test Chapter");
    expect_contains(r, "Lead U.");
}

// ═══════════════════════════════════════════════════════════════════════════
// MemberRoutes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MemberViewSelf) {
    auto r = GET("/members/" + std::to_string(admin_member_id) + "/view", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin U.");
}

TEST_F(IntegrationTest, MemberViewOtherPIIHidden) {
    // Regular member viewing another member -- PII hidden by default (sharing=none)
    auto r = GET("/members/" + std::to_string(admin_member_id) + "/view", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin U.");
    // Email should not be visible since sharing defaults to "none"
    expect_not_contains(r, "admin@test.com");
}

TEST_F(IntegrationTest, MemberDatatableSearch) {
    auto r = POST("/api/members/datatable",
        "draw=1&start=0&length=25&search=Admin",
        admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin U.");
    expect_contains(r, "recordsFiltered");
}

TEST_F(IntegrationTest, MemberDatatableSortByRole) {
    auto r = POST("/api/members/datatable",
        "draw=1&start=0&length=25&search=&order_col_name=role&order_dir=ASC",
        admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "recordsTotal");
}

TEST_F(IntegrationTest, MemberDatatableSortByChapterName) {
    auto r = POST("/api/members/datatable",
        "draw=1&start=0&length=25&search=&order_col_name=chapter_name&order_dir=DESC",
        admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "recordsTotal");
}

// ═══════════════════════════════════════════════════════════════════════════
// SettingsRoutes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, RoleMappingsPageLoadsAdmin) {
    auto r = GET("/settings/roles", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Role");
}

TEST_F(IntegrationTest, RoleMappingsSaveHtmx) {
    auto r = POST_HTMX("/settings/roles",
        "role_discord_id_0=111&role_lug_role_0=admin",
        admin_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, ChannelOptionsApiAdmin) {
    auto r = GET("/api/discord/channel-options", admin_token);
    EXPECT_EQ(r.code, 200);
    // Returns HTML option elements (possibly empty if no guild configured)
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, ForumOptionsApiAdmin) {
    auto r = GET("/api/discord/forum-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, RoleOptionsApiAdmin) {
    auto r = GET("/api/discord/role-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, ChapterOptionsApiAuthenticated) {
    auto r = GET("/api/chapter-options", member_token);
    EXPECT_EQ(r.code, 200);
    // Should include the test chapter
    expect_contains(r, "Permission Test Chapter");
}

// ═══════════════════════════════════════════════════════════════════════════
// EventRoutes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, EventJsonUpdateViaPut) {
    LugEvent e;
    e.title = "JSON Update Event";
    e.start_time = "2026-07-01T00:00:00";
    e.end_time = "2026-07-02T00:00:00";
    e.status = "open";
    e.scope = "lug_wide";
    auto created = event_svc->create(e);

    std::string json = R"({"title":"JSON Updated Event","location":"New Venue"})";
    auto r = PUT_JSON("/events/" + std::to_string(created.id), json, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "success");

    auto found = event_repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->title, "JSON Updated Event");
    EXPECT_EQ(found->location, "New Venue");
}

TEST_F(IntegrationTest, EventCreateWithSuppressDiscord) {
    auto r = POST("/events",
        "title=Suppress+Event&start_time=2026-08-01&end_time=2026-08-02"
        "&scope=lug_wide&suppress_discord=1",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto all = event_repo->find_all();
    bool found = false;
    for (auto& ev : all) {
        if (ev.title == "Suppress Event") {
            EXPECT_TRUE(ev.suppress_discord);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(IntegrationTest, EventEditFormLoadsForAdmin) {
    LugEvent e;
    e.title = "Edit Form Event";
    e.start_time = "2026-07-10T00:00:00";
    e.end_time = "2026-07-11T00:00:00";
    e.status = "open";
    e.scope = "lug_wide";
    auto created = event_svc->create(e);

    auto r = GET("/events/" + std::to_string(created.id) + "/edit", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Edit Form Event");
    expect_contains(r, "Edit Event");
}

// ═══════════════════════════════════════════════════════════════════════════
// MeetingRoutes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MeetingJsonUpdateViaPut) {
    Meeting m;
    m.title = "JSON Update Meeting";
    m.start_time = "2026-06-01T19:00:00";
    m.end_time = "2026-06-01T21:00:00";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    std::string json = R"({"title":"JSON Updated Meeting","location":"Room 42"})";
    auto r = PUT_JSON("/meetings/" + std::to_string(created.id), json, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "success");

    auto found = meeting_repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->title, "JSON Updated Meeting");
    EXPECT_EQ(found->location, "Room 42");
}

TEST_F(IntegrationTest, MeetingCreateWithSuppressDiscord) {
    auto r = POST("/meetings",
        "title=Suppress+Meeting&start_time=2026-08-01T19%3A00"
        "&end_time=2026-08-01T21%3A00&location=Room+S&scope=lug_wide&suppress_discord=1",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto all = meeting_repo->find_all();
    bool found = false;
    for (auto& mtg : all) {
        if (mtg.title == "Suppress Meeting") {
            EXPECT_TRUE(mtg.suppress_discord);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(IntegrationTest, MeetingEditFormLoadsForAdmin) {
    Meeting m;
    m.title = "Edit Form Meeting";
    m.start_time = "2026-06-10T19:00:00";
    m.end_time = "2026-06-10T21:00:00";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    auto r = GET("/meetings/" + std::to_string(created.id) + "/edit", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Edit Form Meeting");
    expect_contains(r, "Edit Meeting");
}

TEST_F(IntegrationTest, MeetingAttendancePanelLoads) {
    Meeting m;
    m.title = "Attendance Panel Test";
    m.start_time = "2026-06-15T19:00:00";
    m.end_time = "2026-06-15T21:00:00";
    m.scope = "lug_wide";
    auto created = meeting_svc->create(m);

    // Check in a member so the panel has data
    attendance_svc->check_in(admin_member_id, "meeting", created.id, "", false);

    auto r = GET("/meetings/" + std::to_string(created.id) + "/attendance", admin_token);
    EXPECT_EQ(r.code, 200);
    // Should contain the attendee info
    expect_contains(r, "Admin U.");
}
