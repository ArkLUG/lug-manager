// Integration tests for the Discord member match review queue: GET requires
// admin or chapter_lead, link/create-new actions resolve the pending row and
// mutate the member correctly, and non-privileged users are rejected.
#include "integration_test_base.hpp"

TEST_F(IntegrationTest, DiscordMatchesPageRequiresAdminOrChapterLead) {
    auto r = GET_HTMX("/settings/discord-matches", member_token);
    // Non-admin/non-lead: full-page GET redirects away, so via HTMX the
    // redirected response should not be the discord-matches page content.
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, DiscordMatchesPageAllowsChapterLead) {
    auto r = GET_HTMX("/settings/discord-matches", chapter_lead_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Discord Matches");
}

TEST_F(IntegrationTest, DiscordMatchesPageAllowsAdmin) {
    auto r = GET_HTMX("/settings/discord-matches", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Discord Matches");
}

// The notification-channel/authorized-roles config form (moved here from the
// old single /settings page) is admin-only - a chapter lead can review and
// resolve matches but must not see or be able to change Discord-wide config.
TEST_F(IntegrationTest, DiscordMatchesPageShowsConfigFormForAdminOnly) {
    auto r_admin = GET_HTMX("/settings/discord-matches", admin_token);
    EXPECT_EQ(r_admin.code, 200);
    expect_contains(r_admin, "Notification Settings");
    expect_contains(r_admin, "Match Notification Channel");

    auto r_lead = GET_HTMX("/settings/discord-matches", chapter_lead_token);
    EXPECT_EQ(r_lead.code, 200);
    expect_not_contains(r_lead, "Notification Settings");
}

// Its own dedicated section endpoint - saving it must not depend on or
// touch any other settings page (Discord/Calendar/Google Calendar).
TEST_F(IntegrationTest, DiscordMatchesConfigSavePersists) {
    auto r = POST_HTMX("/settings/discord-matches",
        "discord_matches_notification_channel_id=notify-chan-999",
        admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(settings_repo->get("discord_matches_notification_channel_id"), "notify-chan-999");
}

TEST_F(IntegrationTest, DiscordMatchesConfigSaveNonAdminForbidden) {
    auto r = POST("/settings/discord-matches",
        "discord_matches_notification_channel_id=hacked",
        chapter_lead_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, DiscordMatchesPageShowsPendingRow) {
    // Seed a discord-less member and a pending match row directly.
    Member m;
    m.discord_user_id = ""; // no discord id
    m.first_name = "Andrew";
    m.last_name = "Hamilton";
    m.display_name = "Andrew Hamilton";
    m.role = "member";
    auto created = member_repo->create(m);

    PendingDiscordMatch p;
    p.discord_user_id      = "discord-999";
    p.discord_username     = "andrewh";
    p.discord_display_name = "Andrew Hamilton";
    p.suggested_member_id  = created.id;
    pending_discord_match_repo->create(p);

    auto r = GET_HTMX("/settings/discord-matches", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Andrew Hamilton");
    expect_contains(r, "andrewh");
}

TEST_F(IntegrationTest, DiscordMatchesLinkResolvesAndSetsDiscordId) {
    Member m;
    m.first_name = "Hal";
    m.last_name = "Miller";
    m.display_name = "Hal Miller";
    m.role = "member";
    auto created = member_repo->create(m);

    PendingDiscordMatch p;
    p.discord_user_id      = "discord-hal-1";
    p.discord_username     = "halm";
    p.discord_display_name = "Hal Miller";
    p.suggested_member_id  = created.id;
    auto pending = pending_discord_match_repo->create(p);

    std::string body = "member_id=" + std::to_string(created.id);
    auto r = POST("/settings/discord-matches/" + std::to_string(pending.id) + "/link", body, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Hal Miller");

    // Member now has the discord id linked
    auto updated = member_repo->find_by_id(created.id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->discord_user_id, "discord-hal-1");
    EXPECT_EQ(updated->discord_username, "halm");

    // Pending row is resolved
    auto resolved = pending_discord_match_repo->find_by_id(pending.id);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->resolved_action, "linked");
    EXPECT_EQ(resolved->resolved_member_id, created.id);

    // No duplicate member was created
    auto no_discord = member_repo->find_without_discord_id();
    for (auto& mm : no_discord) EXPECT_NE(mm.display_name, "Hal Miller");
}

TEST_F(IntegrationTest, DiscordMatchesCreateNewCreatesMemberAndResolves) {
    PendingDiscordMatch p;
    p.discord_user_id      = "discord-newperson-1";
    p.discord_username     = "newperson";
    p.discord_display_name = "New Person";
    // No suggestion at all — a genuinely new person
    auto pending = pending_discord_match_repo->create(p);

    auto r = POST("/settings/discord-matches/" + std::to_string(pending.id) + "/create-new", "", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "New Person");

    auto resolved = pending_discord_match_repo->find_by_id(pending.id);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->resolved_action, "created_new");
    ASSERT_NE(resolved->resolved_member_id, 0);

    auto created_member = member_repo->find_by_id(resolved->resolved_member_id);
    ASSERT_TRUE(created_member.has_value());
    EXPECT_EQ(created_member->discord_user_id, "discord-newperson-1");
    EXPECT_EQ(created_member->display_name, "New Person");
}

TEST_F(IntegrationTest, DiscordMatchesLinkRejectedForRegularMember) {
    Member m;
    m.first_name = "Test";
    m.last_name = "Person";
    m.display_name = "Test Person";
    m.role = "member";
    auto created = member_repo->create(m);

    PendingDiscordMatch p;
    p.discord_user_id      = "discord-test-1";
    p.discord_display_name = "Test Person";
    p.suggested_member_id  = created.id;
    auto pending = pending_discord_match_repo->create(p);

    auto r = POST("/settings/discord-matches/" + std::to_string(pending.id) + "/link",
                   "member_id=" + std::to_string(created.id), member_token);
    EXPECT_EQ(r.code, 403);

    // Not resolved
    auto still_pending = pending_discord_match_repo->find_by_id(pending.id);
    ASSERT_TRUE(still_pending.has_value());
    EXPECT_TRUE(still_pending->resolved_at.empty());
}

TEST_F(IntegrationTest, DiscordMatchesAlreadyResolvedIsTolerant) {
    PendingDiscordMatch p;
    p.discord_user_id      = "discord-already-1";
    p.discord_display_name = "Already Resolved";
    auto pending = pending_discord_match_repo->create(p);
    pending_discord_match_repo->mark_resolved(pending.id, "created_new", 999);

    auto r = POST("/settings/discord-matches/" + std::to_string(pending.id) + "/create-new", "", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Already resolved");
}
