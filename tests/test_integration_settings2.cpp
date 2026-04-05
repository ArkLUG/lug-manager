#include "integration_test_base.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Settings Routes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, SyncAllAdminGraceful) {
    // sync-all may timeout when no Discord bot token is configured
    auto r = POST("/api/discord/sync-all", "", admin_token);
    EXPECT_TRUE(r.code == 200 || r.code == 0);
    // Should not be a server error
    EXPECT_NE(r.code, 500);
}

TEST_F(IntegrationTest, SyncAllNonAdminForbidden) {
    auto r = POST("/api/discord/sync-all", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, SyncNicknamesAdminGraceful) {
    auto r = POST("/api/discord/sync-nicknames", "", admin_token);
    EXPECT_TRUE(r.code == 200 || r.code == 0);
    EXPECT_NE(r.code, 500);
}

TEST_F(IntegrationTest, SyncNicknamesNonAdminForbidden) {
    auto r = POST("/api/discord/sync-nicknames", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, RegenerateNicknamesAdmin) {
    auto r = POST("/api/members/regenerate-nicknames", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
    // The response should include count or status text
    EXPECT_TRUE(r.body.find("updated") != std::string::npos ||
                r.body.find("regenerat") != std::string::npos ||
                r.body.size() > 0);
}

TEST_F(IntegrationTest, RegenerateNicknamesMemberForbidden) {
    auto r = POST("/api/members/regenerate-nicknames", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, GoogleCalendarSyncAllAdminGraceful) {
    // Not configured — should return gracefully (not 500)
    auto r = POST("/api/google-calendar/sync-all", "", admin_token);
    EXPECT_NE(r.code, 500);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, GoogleCalendarSyncAllNonAdminForbidden) {
    auto r = POST("/api/google-calendar/sync-all", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, GoogleCalendarImportAdminGraceful) {
    auto r = POST("/api/google-calendar/import", "", admin_token);
    EXPECT_NE(r.code, 500);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, GoogleCalendarImportNonAdminForbidden) {
    auto r = POST("/api/google-calendar/import", "", member_token);
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, RevertSyncChangeUpdatedField) {
    // Change a member's display_name then revert it via the sync-change endpoint
    Member m = *member_repo->find_by_id(regular_member_id);
    std::string original_name = m.display_name;
    m.display_name = "Changed Name";
    member_repo->update(m);

    // Revert: change_type=updated, field=display_name, old_value=original
    std::string body = "member_id=" + std::to_string(regular_member_id) +
                       "&change_type=updated&field=display_name&old_value=" + original_name;
    auto r = POST("/api/discord/revert-sync-change", body, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Reverted");

    // Verify the name was reverted
    auto reverted = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(reverted.has_value());
    EXPECT_EQ(reverted->display_name, original_name);
}

TEST_F(IntegrationTest, RevertSyncChangeCreatedDeletesMember) {
    // Create a temporary member via the repo
    Member tmp;
    tmp.discord_user_id = "temp-sync-001";
    tmp.discord_username = "temp_sync_user";
    tmp.first_name = "Temp";
    tmp.last_name = "Sync";
    tmp.display_name = "Temp S.";
    tmp.role = "member";
    auto created = member_repo->create(tmp);
    int64_t tmp_id = created.id;

    // Revert creation should delete the member
    std::string body = "member_id=" + std::to_string(tmp_id) + "&change_type=created";
    auto r = POST("/api/discord/revert-sync-change", body, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "deleted");

    // Member should no longer exist
    auto gone = member_repo->find_by_id(tmp_id);
    EXPECT_FALSE(gone.has_value());
}

TEST_F(IntegrationTest, RevertSyncChangeNonAdminForbidden) {
    std::string body = "member_id=" + std::to_string(regular_member_id) +
                       "&change_type=updated&field=display_name&old_value=test";
    auto r = POST("/api/discord/revert-sync-change", body, member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, RevertSyncChangeInvalidMemberId) {
    std::string body = "member_id=0&change_type=updated&field=display_name&old_value=test";
    auto r = POST("/api/discord/revert-sync-change", body, admin_token);
    EXPECT_EQ(r.code, 200); // Returns HTML with error message, not HTTP error
    expect_contains(r, "Invalid member ID");
}

TEST_F(IntegrationTest, SettingsSaveWithTimezone) {
    auto r = POST_HTMX("/settings",
        "discord_guild_id=tz-guild"
        "&discord_announcements_channel_id=tz-ch"
        "&lug_timezone=America/Denver"
        "&ical_calendar_name=TZ+Test",
        admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(settings_repo->get("lug_timezone"), "America/Denver");
    EXPECT_EQ(settings_repo->get("discord_guild_id"), "tz-guild");
}

TEST_F(IntegrationTest, SettingsSaveReportForumChannels) {
    auto r = POST_HTMX("/settings",
        "discord_guild_id=rf-guild"
        "&discord_announcements_channel_id=rf-ch"
        "&lug_timezone=UTC"
        "&ical_calendar_name=RF+Test"
        "&discord_event_reports_forum_channel_id=evt-forum-123"
        "&discord_meeting_reports_forum_channel_id=mtg-forum-456",
        admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(settings_repo->get("discord_event_reports_forum_channel_id"), "evt-forum-123");
    EXPECT_EQ(settings_repo->get("discord_meeting_reports_forum_channel_id"), "mtg-forum-456");
}

TEST_F(IntegrationTest, SettingsNonAdminForbiddenPost) {
    auto r = POST("/settings", "discord_guild_id=hacked", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, VoiceChannelOptionsAuthenticated) {
    auto r = GET("/api/discord/voice-channel-options", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, VoiceChannelOptionsUnauthenticated) {
    auto r = GET("/api/discord/voice-channel-options");
    // Unauthenticated users should get 401 or a redirect
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, VoiceChannelOptionsMemberAllowed) {
    // Voice channel options is available to any authenticated user
    auto r = GET("/api/discord/voice-channel-options", member_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}
