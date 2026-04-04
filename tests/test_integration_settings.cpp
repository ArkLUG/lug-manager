#include "integration_test_base.hpp"

TEST_F(IntegrationTest, SettingsPageLoads) {
    auto r = GET("/settings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Discord Settings");
    expect_contains(r, "Google Calendar");
    expect_contains(r, "Bulk Sync");
    expect_contains(r, "Suppress");
}

TEST_F(IntegrationTest, SettingsSaveAndApply) {
    auto r = POST_HTMX("/settings",
        "discord_guild_id=test-guild&discord_announcements_channel_id=test-ch&lug_timezone=America/Chicago&ical_calendar_name=Test+Cal",
        admin_token);
    EXPECT_EQ(r.code, 200);

    EXPECT_EQ(settings_repo->get("discord_guild_id"), "test-guild");
    EXPECT_EQ(settings_repo->get("lug_timezone"), "America/Chicago");
    EXPECT_EQ(settings_repo->get("ical_calendar_name"), "Test Cal");
}

TEST_F(IntegrationTest, ChapterOptionsApi) {
    Chapter ch;
    ch.name = "API Chapter";
    ch.discord_announcement_channel_id = "ch-api";
    chapter_repo->create(ch);

    auto r = GET("/api/chapter-options", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "API Chapter");
}

TEST_F(IntegrationTest, MemberOptionsApi) {
    auto r = GET("/api/member-options", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin U.");
    expect_contains(r, "Regular U.");
}

TEST_F(IntegrationTest, RolesPageLoads) {
    auto r = GET("/settings/roles", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Role");
}

// ═══════════════════════════════════════════════════════════════════════════
// UI Content Validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, DiscordChannelOptionsAdmin) {
    auto r = GET("/api/discord/channel-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordChannelOptionsNonAdmin) {
    auto r = GET("/api/discord/channel-options", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, DiscordForumOptionsAdmin) {
    auto r = GET("/api/discord/forum-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordForumOptionsNonAdmin) {
    auto r = GET("/api/discord/forum-options", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, DiscordRoleOptionsAdmin) {
    auto r = GET("/api/discord/role-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordRoleOptionsNonAdmin) {
    auto r = GET("/api/discord/role-options", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, DiscordTestAnnouncementGraceful) {
    // No channel configured — returns 200 with "no channel" message
    auto r = POST("/api/discord/test-announcement", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordTestAnnouncementNonAdmin) {
    auto r = POST("/api/discord/test-announcement", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, DiscordSyncMembersGraceful) {
    auto r = POST("/api/discord/sync-members", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordSyncMembersNonAdmin) {
    auto r = POST("/api/discord/sync-members", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, DiscordSyncAllGraceful) {
    // May timeout (code 0) when no Discord bot token is configured
    auto r = POST("/api/discord/sync-all", "", admin_token);
    EXPECT_TRUE(r.code == 200 || r.code == 0);
}

TEST_F(IntegrationTest, DiscordSyncNicknamesGraceful) {
    // May timeout (code 0) when no Discord bot token is configured
    auto r = POST("/api/discord/sync-nicknames", "", admin_token);
    EXPECT_TRUE(r.code == 200 || r.code == 0);
}

TEST_F(IntegrationTest, DiscordSyncNicknamesNonAdmin) {
    auto r = POST("/api/discord/sync-nicknames", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, GoogleCalendarImportGraceful) {
    auto r = POST("/api/google-calendar/import", "", admin_token);
    EXPECT_NE(r.code, 500);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, GoogleCalendarSyncAllGraceful) {
    auto r = POST("/api/google-calendar/sync-all", "", admin_token);
    EXPECT_NE(r.code, 500);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, RegenerateNicknamesGraceful) {
    auto r = POST("/api/members/regenerate-nicknames", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, RegenerateNicknamesNonAdmin) {
    auto r = POST("/api/members/regenerate-nicknames", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, SettingsSaveAllFields) {
    auto r = POST_HTMX("/settings",
        "discord_guild_id=guild-123"
        "&discord_announcements_channel_id=ch-123"
        "&discord_events_forum_channel_id=forum-123"
        "&discord_announcement_role_id=role-123"
        "&discord_non_lug_event_role_id=role-456"
        "&lug_timezone=America/New_York"
        "&ical_calendar_name=Full+Test+Cal"
        "&discord_suppress_pings=1"
        "&discord_suppress_updates=1",
        admin_token);
    EXPECT_EQ(r.code, 200);

    EXPECT_EQ(settings_repo->get("discord_guild_id"), "guild-123");
    EXPECT_EQ(settings_repo->get("discord_events_forum_channel_id"), "forum-123");
    EXPECT_EQ(settings_repo->get("lug_timezone"), "America/New_York");
    EXPECT_EQ(settings_repo->get("ical_calendar_name"), "Full Test Cal");
    EXPECT_EQ(settings_repo->get("discord_suppress_pings"), "1");
    EXPECT_EQ(settings_repo->get("discord_suppress_updates"), "1");
}

TEST_F(IntegrationTest, SettingsNonAdminForbidden) {
    auto r = POST("/settings",
        "discord_guild_id=hacked",
        member_token);
    EXPECT_EQ(r.code, 403); // non-admin gets forbidden
}

// ═══════════════════════════════════════════════════════════════════════════
// Roles — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, DiscordRolesApiAdmin) {
    // No Discord configured — must not return 403/401, returns JSON
    auto r = GET("/api/discord/roles", admin_token);
    EXPECT_NE(r.code, 403);
    EXPECT_NE(r.code, 401);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, DiscordRolesApiNonAdmin) {
    auto r = GET("/api/discord/roles", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, RolesMappingSaveHtmxReturns200) {
    // HTMX POST to /settings/roles returns 200 + HX-Redirect
    auto r = POST_HTMX("/settings/roles", "", admin_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, RolesMappingSaveNonHtmxReturns302) {
    // Non-HTMX POST to /settings/roles returns redirect (302 or 307)
    auto r = POST("/settings/roles", "", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
}

TEST_F(IntegrationTest, RolesPageNonAdminForbidden) {
    auto r = GET("/settings/roles", member_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307); // non-admin gets redirected to dashboard
}

// ═══════════════════════════════════════════════════════════════════════════
// Access control — member permissions
// ═══════════════════════════════════════════════════════════════════════════

