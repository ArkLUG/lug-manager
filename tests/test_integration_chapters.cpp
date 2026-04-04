#include "integration_test_base.hpp"

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

