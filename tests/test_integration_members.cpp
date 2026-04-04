#include "integration_test_base.hpp"

TEST_F(IntegrationTest, MembersPageLoads) {
    auto r = GET("/members", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Members");
    expect_contains(r, "members-table");
}

TEST_F(IntegrationTest, MembersNewForm) {
    auto r = GET("/members/new", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "First Name");
    expect_contains(r, "Last Name");
    expect_contains(r, "Discord User ID");
}

TEST_F(IntegrationTest, MembersCreateAndDelete) {
    auto r = POST("/members",
        "discord_user_id=test-create-001&discord_username=newuser&first_name=John&last_name=Smith&role=member",
        admin_token);
    EXPECT_EQ(r.code, 200);

    // Find the member
    auto found = member_repo->find_by_discord_id("test-create-001");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->first_name, "John");
    EXPECT_EQ(found->last_name, "Smith");
    EXPECT_EQ(found->display_name, "John S.");

    // Delete
    auto dr = DEL("/members/" + std::to_string(found->id), "", admin_token);
    EXPECT_EQ(dr.code, 200);
    EXPECT_FALSE(member_repo->find_by_id(found->id).has_value());
}

TEST_F(IntegrationTest, MembersEditForm) {
    auto r = GET("/members/" + std::to_string(admin_member_id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin");
    expect_contains(r, "User");
}

TEST_F(IntegrationTest, MembersUpdatePost) {
    auto r = POST("/members/" + std::to_string(admin_member_id),
        "first_name=Updated&last_name=Admin&discord_username=admin_user&email=admin@test.com&role=admin",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto found = member_repo->find_by_id(admin_member_id);
    EXPECT_EQ(found->first_name, "Updated");
    EXPECT_EQ(found->display_name, "Updated A.");
}

TEST_F(IntegrationTest, MembersDatatableApi) {
    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "recordsTotal");
    expect_contains(r, "Admin U.");
}

TEST_F(IntegrationTest, MembersNonAdminCannotCreate) {
    auto r = POST("/members",
        "discord_user_id=blocked&discord_username=blocked&first_name=No&last_name=Access&role=member",
        member_token);
    EXPECT_EQ(r.code, 403);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chapters
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MembersUpdatePutJson) {
    std::string json = R"({"email":"admin@json.com","role":"admin"})";
    auto r = PUT_JSON("/members/" + std::to_string(admin_member_id), json, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "success");

    auto found = member_repo->find_by_id(admin_member_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->email, "admin@json.com");
}

TEST_F(IntegrationTest, MembersPutJsonNonAdminForbidden) {
    std::string json = R"({"role":"admin"})";
    auto r = PUT_JSON("/members/" + std::to_string(regular_member_id), json, member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, MemberSetPaidStatus) {
    auto r = POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=2026-12-31", admin_token);
    EXPECT_EQ(r.code, 200);

    auto found = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->is_paid);
    EXPECT_EQ(found->paid_until, "2026-12-31");
}

TEST_F(IntegrationTest, MemberClearPaidStatus) {
    // Mark paid first
    POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=2026-12-31", admin_token);

    // Clear
    auto r = POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=", admin_token);
    EXPECT_EQ(r.code, 200);

    auto found = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->is_paid);
}

TEST_F(IntegrationTest, MemberSetPaidNonAdminForbidden) {
    auto r = POST("/members/" + std::to_string(regular_member_id) + "/paid",
        "paid_until=2026-12-31", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, MembersDeleteNonAdminForbidden) {
    auto r = DEL("/members/" + std::to_string(admin_member_id), "", member_token);
    EXPECT_EQ(r.code, 403);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chapters — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

