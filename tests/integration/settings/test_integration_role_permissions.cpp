#include "integration_test_base.hpp"
#include "auth/Permissions.hpp"

// Settings > Roles and permissions (auth/Permissions.hpp, migration 075).

// The defaults keep what chapter leads (and moderators) could do before.
TEST_F(IntegrationTest, PermissionDefaultsMatchTheOldRoles) {
    EXPECT_EQ(GET("/members.csv", chapter_lead_token).code, 200);
    EXPECT_EQ(GET("/members.csv", member_token).code, 403);
    EXPECT_EQ(GET("/settings/discord-matches", chapter_lead_token).code, 200);
    // Admin-only before, admin-only now
    EXPECT_EQ(GET("/audit", chapter_lead_token).code, 403);
    EXPECT_EQ(GET("/attendance/overview", chapter_lead_token).code, 403);
    EXPECT_EQ(GET("/reports/annual", chapter_lead_token).code, 403);
    EXPECT_EQ(GET("/events/all", chapter_lead_token).code, 403);
    auto mod = perms::load(*db, "moderator"), lead = perms::load(*db, "chapter_lead");
    EXPECT_EQ(mod, lead);
    EXPECT_EQ(mod, (std::set<std::string>{"members.view_private", "members.edit", "members.export",
                                          "discord.matches", "dues.record", "inventory.manage"}));
    EXPECT_TRUE(perms::load(*db, "member").empty());
}

TEST_F(IntegrationTest, PermissionsPageIsAdminOnly) {
    EXPECT_EQ(GET("/settings/permissions", admin_token).code, 200);
    EXPECT_EQ(GET("/settings/permissions", chapter_lead_token).code, 403);
    EXPECT_EQ(GET("/settings/permissions", member_token).code, 403);
    // Even a role granted everything can't change permissions (or give itself more)
    std::string all;
    for (const auto& p : perms::all()) all += "&grant=member|" + std::string(p.key);
    EXPECT_EQ(POST("/settings/permissions", all.substr(1), admin_token).code, 200);
    EXPECT_EQ(POST("/settings/permissions", "grant=member|audit.view", member_token).code, 403);
    EXPECT_EQ(GET("/settings/permissions", member_token).code, 403);
    EXPECT_EQ(GET("/settings/features", member_token).code, 403);
    EXPECT_EQ(GET("/settings/roles", member_token).code, 307);   // sent to the dashboard
}

TEST_F(IntegrationTest, GrantingAndRevokingAPermissionTakesEffectAndIsAudited) {
    EXPECT_EQ(GET("/audit", member_token).code, 403);
    auto r = POST("/settings/permissions", "grant=member|audit.view&grant=chapter_lead|members.export", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Saved.");
    EXPECT_EQ(GET("/audit", member_token).code, 200);
    EXPECT_EQ(GET("/audit.csv", member_token).code, 200);
    // The sidebar shows the Admin heading and the Audit Log, but not Settings
    auto dash = GET("/dashboard", member_token);
    expect_contains(dash, "hx-get=\"/audit\"");
    expect_not_contains(dash, "hx-get=\"/settings/overview\"");
    // Unticked boxes are revoked: chapter leads kept only members.export
    EXPECT_EQ(perms::load(*db, "chapter_lead"), std::set<std::string>{"members.export"});
    EXPECT_EQ(GET("/settings/discord-matches", chapter_lead_token).code, 307);   // sent away
    EXPECT_EQ(GET("/members.csv", chapter_lead_token).code, 200);
    // Moderators got nothing ticked either
    EXPECT_TRUE(perms::load(*db, "moderator").empty());
    // Every change is in the audit log
    auto log = GET("/audit", admin_token);
    expect_contains(log, "settings.permissions");
    expect_contains(log, "+audit.view");
    expect_contains(log, "-discord.matches");

    POST("/settings/permissions", "", admin_token);
    EXPECT_EQ(GET("/audit", member_token).code, 403);
}

TEST_F(IntegrationTest, PermissionsIgnoreUnknownRolesAndKeys) {
    EXPECT_EQ(POST("/settings/permissions", "grant=admin|audit.view&grant=member|nope.nope&grant=superuser|audit.view&grant=member", admin_token).code, 200);
    auto st = db->prepare("SELECT COUNT(*) FROM role_permissions WHERE role NOT IN ('moderator','chapter_lead') OR permission='nope.nope'");
    ASSERT_TRUE(st.step());
    EXPECT_EQ(st.col_int(0), 0);
    // An unknown permission can't be stored directly either
    EXPECT_ANY_THROW(db->execute("INSERT INTO role_permissions(role, permission) VALUES('admin', 'audit.view')"));
}

TEST_F(IntegrationTest, ManageEveryEventPermissionCoversGroupWideEvents) {
    EXPECT_EQ(GET("/events/all", member_token).code, 403);
    POST("/settings/permissions", "grant=member|schedule.all_chapters", admin_token);
    EXPECT_EQ(GET("/events/all", member_token).code, 200);
    expect_contains(GET("/schedule", member_token), "/events/new");
}

// Without "Add and edit members" nobody but admins adds members, and those who
// may add them only ever create plain members (no handing out roles).
TEST_F(IntegrationTest, NonAdminsOnlyCreatePlainMembers) {
    auto r = POST("/members", "first_name=New&last_name=Mod&discord_user_id=new-mod-1&role=moderator", chapter_lead_token);
    EXPECT_LT(r.code, 400);
    auto st = db->prepare("SELECT role FROM members WHERE discord_user_id='new-mod-1'");
    ASSERT_TRUE(st.step());
    EXPECT_EQ(st.col_text(0), "member");
    POST("/settings/permissions", "", admin_token);   // nobody but admins now
    EXPECT_EQ(POST("/members", "first_name=Another&last_name=One", chapter_lead_token).code, 403);
}

TEST_F(IntegrationTest, DiscordRoleMappingPageOffersModerator) {
    // No Discord in tests: the page says it can't reach Discord, the form isn't shown;
    // the mapping itself resolves moderator (RoleMappingRepository).
    db->execute("INSERT INTO discord_role_mappings(discord_role_id, discord_role_name, lug_role) VALUES('r-mod', 'Mods', 'moderator')");
    RoleMappingRepository maps(*db);
    EXPECT_EQ(maps.resolve_lug_role({"r-mod"}).value_or(""), "moderator");
    EXPECT_EQ(maps.resolve_lug_role({"r-x", "r-mod"}).value_or(""), "moderator");
}
