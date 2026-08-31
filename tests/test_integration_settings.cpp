#include "integration_test_base.hpp"

// Settings is split into separate pages (Discord / Calendar / Google Calendar /
// Discord Matches), each its own route + template + save endpoint, so a save
// on one page can never touch another page's settings - see SettingsRoutes.cpp
// and DiscordMatchRoutes.cpp for the full rationale.
TEST_F(IntegrationTest, SettingsPageLoads) {
    auto r = GET("/settings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Discord Settings");
    expect_contains(r, "Bulk Sync");
    expect_contains(r, "Suppress");
}

TEST_F(IntegrationTest, SettingsCalendarPageLoads) {
    auto r = GET("/settings/calendar", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Calendar Name");
    expect_contains(r, "Timezone");
}

TEST_F(IntegrationTest, SettingsCalendarPageNonAdminForbidden) {
    auto r = GET("/settings/calendar", member_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307); // non-admin gets redirected to dashboard
}

TEST_F(IntegrationTest, SettingsGoogleCalendarPageLoads) {
    auto r = GET("/settings/google-calendar", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Service Account JSON Path");
    expect_contains(r, "Google Calendar ID");
}

TEST_F(IntegrationTest, SettingsGoogleCalendarPageNonAdminForbidden) {
    auto r = GET("/settings/google-calendar", member_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307); // non-admin gets redirected to dashboard
}

TEST_F(IntegrationTest, SettingsCalendarSaveOnly) {
    auto r = POST_HTMX("/settings/calendar",
        "lug_timezone=Asia/Tokyo&ical_calendar_name=Tokyo+Cal",
        admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(settings_repo->get("lug_timezone"), "Asia/Tokyo");
    EXPECT_EQ(settings_repo->get("ical_calendar_name"), "Tokyo Cal");
}

TEST_F(IntegrationTest, SettingsGoogleCalendarSaveOnly) {
    auto r = POST_HTMX("/settings/google-calendar",
        "google_service_account_json_path=/etc/sa.json"
        "&google_calendar_id=cal-id@group.calendar.google.com",
        admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(settings_repo->get("google_service_account_json_path"), "/etc/sa.json");
    EXPECT_EQ(settings_repo->get("google_calendar_id"), "cal-id@group.calendar.google.com");
}

TEST_F(IntegrationTest, SettingsSaveAndApply) {
    // Settings is split into per-section forms/endpoints (see SettingsRoutes.cpp) -
    // Discord-server fields and calendar fields belong to different sections now.
    auto r1 = POST_HTMX("/settings/discord",
        "discord_guild_id=test-guild&discord_announcements_channel_id=test-ch",
        admin_token);
    EXPECT_EQ(r1.code, 200);
    auto r2 = POST_HTMX("/settings/calendar",
        "lug_timezone=America/Chicago&ical_calendar_name=Test+Cal",
        admin_token);
    EXPECT_EQ(r2.code, 200);

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

// Security regression: /api/member-options builds a raw <option> HTML
// fragment from struct fields via std::ostringstream, not a mustache
// {{tag}} - two distinct injection points were unescaped: the caller-
// supplied `placeholder` query param (reflected XSS - no stored data
// needed at all, just a crafted link to this endpoint) and each member's
// display_name (stored XSS - display_name is set directly from Discord's
// user-controlled global_name field on login/auto-provisioning with no
// sanitization, so any authenticated member - including one who was only
// ever auto-provisioned, never manually approved - can plant this).
TEST_F(IntegrationTest, MemberOptionsApiEscapesReflectedPlaceholder) {
    auto r = GET("/api/member-options?placeholder=" +
                 std::string("%3Cscript%3Ealert(1)%3C%2Fscript%3E"), admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.body.find("<script>"), std::string::npos);
    expect_contains(r, "&lt;script&gt;");
}

TEST_F(IntegrationTest, MemberOptionsApiEscapesStoredDisplayName) {
    Member m;
    m.discord_user_id = "xss-test-001";
    m.discord_username = "xsstest";
    m.display_name = "<script>alert(1)</script>";
    m.role = "member";
    member_repo->create(m);

    auto r = GET("/api/member-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.body.find("<script>alert(1)</script>"), std::string::npos);
    expect_contains(r, "&lt;script&gt;alert(1)&lt;/script&gt;");
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
    auto r1 = POST_HTMX("/settings/discord",
        "discord_guild_id=guild-123"
        "&discord_announcements_channel_id=ch-123"
        "&discord_events_forum_channel_id=forum-123"
        "&discord_announcement_role_id=role-123"
        "&discord_non_lug_event_role_id=role-456"
        "&discord_suppress_pings=1"
        "&discord_suppress_updates=1",
        admin_token);
    EXPECT_EQ(r1.code, 200);
    auto r2 = POST_HTMX("/settings/calendar",
        "lug_timezone=America/New_York"
        "&ical_calendar_name=Full+Test+Cal",
        admin_token);
    EXPECT_EQ(r2.code, 200);

    EXPECT_EQ(settings_repo->get("discord_guild_id"), "guild-123");
    EXPECT_EQ(settings_repo->get("discord_events_forum_channel_id"), "forum-123");
    EXPECT_EQ(settings_repo->get("lug_timezone"), "America/New_York");
    EXPECT_EQ(settings_repo->get("ical_calendar_name"), "Full Test Cal");
    EXPECT_EQ(settings_repo->get("discord_suppress_pings"), "1");
    EXPECT_EQ(settings_repo->get("discord_suppress_updates"), "1");
}

TEST_F(IntegrationTest, SettingsNonAdminForbidden) {
    auto r = POST("/settings/discord",
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

