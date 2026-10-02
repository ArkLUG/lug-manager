// Discord behaviour against a local fake Discord API (tests/fake_discord.hpp).
// Nothing here can reach the real Discord: LUG_OFFLINE=1 blocks everything
// that isn't loopback, and the fake listens on 127.0.0.1 only.
#include "integration_test_base.hpp"
#include "fake_discord.hpp"
#include "services/Features.hpp"
#include "services/notifications/ReminderService.hpp"

namespace {
const std::string LUG_CH = "600000000000000001", FORUM_CH = "600000000000000002";
const std::string ADMIN_ROLE = "500000000000000001", MEMBER_ROLE = "500000000000000002", LEAD_ROLE = "500000000000000003";
}

class DiscordFakeTest : public IntegrationTest {
protected:
    std::unique_ptr<FakeDiscord> fake;
    void SetUp() override {
        IntegrationTest::SetUp();
        fake = std::make_unique<FakeDiscord>();
        discord_client->reconfigure(fake->guild_id, LUG_CH, FORUM_CH, "", "", "America/Chicago");
        fake->channels = {{{"id", LUG_CH}, {"name", "announcements"}, {"type", 0}},
                          {{"id", FORUM_CH}, {"name", "events"}, {"type", 15}}};
        fake->roles = {{ADMIN_ROLE, "Admins"}, {MEMBER_ROLE, "LUG Member"}, {LEAD_ROLE, "Chapter Lead"}};
        role_mapping_repo->upsert(ADMIN_ROLE, "Admins", "admin");
        role_mapping_repo->upsert(MEMBER_ROLE, "LUG Member", "member");
    }
    void TearDown() override {
        fake.reset();
        IntegrationTest::TearDown();
    }
};

TEST_F(DiscordFakeTest, OfflineStillBlocksTheRealDiscord) {
    unsetenv("LUG_DISCORD_BASE");
    discord_client->clear_cache();
    // Real discord.com is blocked by LUG_OFFLINE: the request fails before leaving the machine.
    EXPECT_THROW(discord_client->fetch_guild_members(), std::exception);
    EXPECT_TRUE(fake->requests().empty());
}

TEST_F(DiscordFakeTest, MemberSyncImportsUpdatesAndSkips) {
    fake->add_member("100000000000000001", "newadmin", {ADMIN_ROLE});
    fake->add_member("100000000000000002", "newmember", {MEMBER_ROLE}, false, "Nick Name");
    fake->add_member("100000000000000003", "visitor", {});                 // no mapped role -> plain member
    fake->add_member("member-test-001", "regular_renamed", {MEMBER_ROLE}); // existing fixture member
    auto r = member_sync_svc->sync_from_guild();
    EXPECT_EQ(r.errors, 0) << r.error_message;
    EXPECT_EQ(r.imported, 3);        // every server member is a LUG member; roles come from mappings
    auto admin = member_repo->find_by_discord_id("100000000000000001");
    ASSERT_TRUE(admin.has_value());
    EXPECT_EQ(admin->role, "admin");
    auto nick = member_repo->find_by_discord_id("100000000000000002");
    ASSERT_TRUE(nick.has_value());
    EXPECT_EQ(nick->role, "member");
    EXPECT_EQ(member_repo->find_by_discord_id("100000000000000003")->role, "member");
    EXPECT_FALSE(member_repo->find_by_discord_id("800000000000000099").has_value());   // the bot
    EXPECT_EQ(member_repo->find_by_id(regular_member_id)->discord_username, "regular_renamed");
    // Read-only: the sync didn't write anything to Discord
    EXPECT_TRUE(fake->matching("^(PUT|POST|PATCH|DELETE) ").empty());
}

TEST_F(DiscordFakeTest, MemberSyncDemotesDiscordRolesOfPeopleWhoLeft) {
    // Two elevated members: one got admin from Discord, one was made admin by hand.
    fake->add_member("100000000000000010", "stays", {MEMBER_ROLE});
    Member a; a.discord_user_id = "100000000000000011"; a.first_name = "Gone"; a.last_name = "Discord";
    a.display_name = "Gone D."; a.role = "admin";
    int64_t gone = member_repo->create(a).id;
    member_repo->set_role_source(gone, "discord");
    Member b = a; b.discord_user_id = "100000000000000012"; b.display_name = "Gone M.";
    int64_t manual = member_repo->create(b).id;
    member_repo->set_role_source(manual, "manual");

    member_sync_svc->sync_from_guild();
    EXPECT_EQ(member_repo->find_by_id(gone)->role, "member");     // Discord-sourced: follows the server
    EXPECT_EQ(member_repo->find_by_id(manual)->role, "admin");    // manual: never lowered by sync
}

TEST_F(DiscordFakeTest, MemberSyncEmptyListChangesNothing) {
    {
        auto u = db->prepare("UPDATE chapters SET discord_lead_role_id=? WHERE id=?");
        u.bind(1, LEAD_ROLE); u.bind(2, test_chapter_id); u.step();
    }
    fake->add_member("lead-test-001", "lead", {MEMBER_ROLE});
    fake->members_empty = true;
    auto r = member_sync_svc->sync_from_guild();
    EXPECT_EQ(r.errors, 1);
    EXPECT_TRUE(fake->matching("^PUT ").empty());      // no role pushed to anyone
}

TEST_F(DiscordFakeTest, ChapterLeadRoleSyncBothWays) {
    {
        auto u = db->prepare("UPDATE chapters SET discord_lead_role_id=? WHERE id=?");
        u.bind(1, LEAD_ROLE); u.bind(2, test_chapter_id); u.step();
    }
    // Web lead (fixture) lacks the Discord role -> app gives it to them on Discord
    fake->add_member("lead-test-001", "lead", {MEMBER_ROLE});
    // Discord holder of the lead role who isn't a web lead -> promoted in the app
    fake->add_member("member-test-001", "regular", {MEMBER_ROLE, LEAD_ROLE});
    member_sync_svc->sync_from_guild();
    EXPECT_EQ(fake->member_roles("lead-test-001").count(LEAD_ROLE), 1u);
    auto role = chapter_member_repo->get_chapter_role(regular_member_id, test_chapter_id);
    ASSERT_TRUE(role.has_value());
    EXPECT_EQ(*role, "lead");
    // With chapters switched off, none of that happens
    fake->clear();
    Features::set("chapters", false);
    fake->add_member("em-test-001", "em", {MEMBER_ROLE, LEAD_ROLE});
    member_sync_svc->sync_from_guild();
    EXPECT_FALSE(chapter_member_repo->get_chapter_role(event_manager_member_id, test_chapter_id).value_or("") == "lead");
    EXPECT_TRUE(fake->matching("^PUT ").empty());
}

TEST_F(DiscordFakeTest, LoginChecksGuildMembershipAndMapsRoles) {
    auto start = GET("/auth/login");
    std::string nonce;
    {
        auto p = start.headers.find("oauth_state=");
        ASSERT_NE(p, std::string::npos);
        nonce = start.headers.substr(p + 12, start.headers.find(';', p) - p - 12);
    }
    auto callback = [&](const std::string& user_id) {
        fake->oauth_user_id = user_id;
        return http("GET", "/auth/callback?code=abc&state=" + nonce, "", "", false, "", false,
                    {"Cookie: oauth_state=" + nonce});
    };
    // Not in the server -> refused, nothing created
    auto refused = callback("100000000000000020");
    EXPECT_NE(refused.location.find("error=not_member"), std::string::npos);
    EXPECT_FALSE(member_repo->find_by_discord_id("100000000000000020").has_value());
    // In the server with the admin role -> provisioned as admin, signed in
    fake->add_member("100000000000000021", "oauthuser", {ADMIN_ROLE});
    auto ok = callback("100000000000000021");
    EXPECT_NE(ok.location.find("/dashboard"), std::string::npos);
    EXPECT_NE(ok.headers.find("session="), std::string::npos);
    auto m = member_repo->find_by_discord_id("100000000000000021");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->role, "admin");
    EXPECT_FALSE(fake->matching("POST /api/oauth2/token").empty());
}

TEST_F(DiscordFakeTest, EventPublishingNeverPingsEveryone) {
    auto r = POST("/events", "title=Show+%40everyone&start_time=2099-05-01&end_time=2099-05-01&scope=lug_wide"
                             "&location=Expo", admin_token);
    EXPECT_EQ(r.code == 200 || r.code == 303, true) << r.code << r.body;
    auto posts = fake->matching("^POST /api/v10/(channels/[^/]+/(messages|threads)|guilds/[^/]+/scheduled-events)");
    ASSERT_FALSE(posts.empty());
    for (const auto& p : fake->matching("^POST /api/v10/channels/")) {
        auto b = nlohmann::json::parse(p.body, nullptr, false);
        ASSERT_TRUE(b.is_object()) << p.path << " " << p.body;
        // Forum posts carry the text in their starter "message"
        const auto& msg = b.contains("message") ? b["message"] : b;
        if (!msg.contains("content")) continue;          // e.g. naming a thread
        ASSERT_TRUE(msg.contains("allowed_mentions")) << p.body;
        auto parse = msg["allowed_mentions"].value("parse", nlohmann::json::array());
        for (const auto& x : parse) EXPECT_NE(x.get<std::string>(), "everyone") << p.body;
    }
}

TEST_F(DiscordFakeTest, WaitlistPromotionSendsOneDm) {
    LugEvent e;
    e.title = "Tiny"; e.start_time = "2099-08-01T09:00:00"; e.end_time = "2099-08-01T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    e.max_attendees = 1;
    auto ev = event_svc->create(e);
    std::string url = "/events/" + std::to_string(ev.id) + "/rsvp";
    POST(url, "", admin_token);
    POST(url, "", member_token);       // waitlisted
    fake->clear();
    POST(url, "", admin_token);        // admin leaves -> regular member promoted
    ASSERT_TRUE(fake->wait_for("POST /api/v10/channels/dmmember-test-001/messages"));
    auto open = fake->matching("POST /api/v10/users/@me/channels");
    ASSERT_EQ(open.size(), 1u);
    EXPECT_NE(open[0].body.find("member-test-001"), std::string::npos);
    auto msg = fake->matching("POST /api/v10/channels/dmmember-test-001/messages");
    EXPECT_NE(msg[0].body.find("Tiny"), std::string::npos);
}

TEST_F(DiscordFakeTest, RemindersPostOnceToTheLugChannel) {
    settings_repo->set("discord_reminders_enabled", "1");
    settings_repo->set("discord_reminder_hours", "48");
    LugEvent e;
    std::time_t t = std::time(nullptr) + 24 * 3600;
    std::tm tm{}; localtime_r(&t, &tm);
    char buf[32]; std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    e.title = "Soon Show"; e.start_time = buf; e.end_time = buf;
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    {   // allow Discord for this one (the create above skipped publishing)
        auto u = db->prepare("UPDATE lug_events SET suppress_discord=0 WHERE id=?");
        u.bind(1, ev.id); u.step();
    }
    ReminderService rs(*db, *meeting_repo, *event_repo, *chapter_repo, *member_repo, *settings_repo, *discord_client);
    rs.set_notifier(notifier_with_chat());
    discord_client->set_timezone("America/Chicago");
    auto r1 = rs.run_once();
    EXPECT_EQ(r1.events, 1);
    EXPECT_EQ(fake->matching("POST /api/v10/channels/" + LUG_CH + "/messages").size(), 1u);
    rs.run_once();                                       // claimed: not sent twice
    EXPECT_EQ(fake->matching("POST /api/v10/channels/" + LUG_CH + "/messages").size(), 1u);
}

TEST_F(DiscordFakeTest, RateLimitIsRetried) {
    fake->add_member("100000000000000030", "x", {MEMBER_ROLE});
    fake->rate_limit_next = 1;
    auto list = discord_client->fetch_guild_members();
    EXPECT_FALSE(list.empty());
    EXPECT_GE(fake->matching("GET /api/v10/guilds/[^/]+/members").size(), 2u);
}

TEST_F(DiscordFakeTest, UnsafeIdsNeverReachDiscord) {
    Member m; m.discord_user_id = "../../channels/1"; m.first_name = "Bad"; m.last_name = "Id"; m.display_name = "Bad I.";
    m.role = "member";
    auto created = member_repo->create(m);
    EXPECT_FALSE(discord_client->send_dm(created.discord_user_id, "hi"));
    EXPECT_TRUE(fake->matching("channels/1").empty());
}

// Discord down (or unreachable): the settings pages still load, keep the saved
// choices and say so; saving role mappings changes nothing.
TEST_F(DiscordFakeTest, PagesLoadWhenDiscordUnreachable) {
    unsetenv("LUG_DISCORD_BASE");   // real discord.com, which LUG_OFFLINE blocks
    discord_client->clear_cache();
    auto s = GET("/settings", admin_token);
    EXPECT_EQ(s.code, 200);
    expect_contains(s, "Couldn't reach Discord");
    expect_contains(s, "(saved: " + LUG_CH + ")");                  // the saved channel stays selected
    auto opts = GET("/api/discord/role-options?selected=" + ADMIN_ROLE, admin_token);
    EXPECT_EQ(opts.code, 200);
    expect_contains(opts, "Couldn't reach Discord");
    expect_contains(opts, "value=\"" + ADMIN_ROLE + "\" selected");
    auto roles = GET("/settings/roles", admin_token);
    EXPECT_EQ(roles.code, 200);
    expect_contains(roles, "Couldn't reach Discord");
    EXPECT_EQ(POST("/settings/roles", "", admin_token).code, 502);
    EXPECT_EQ(role_mapping_repo->find_all().size(), 2u);             // nothing removed
}

// Signing in with Discord fills in a missing email from the Discord account's
// verified one - never an unverified one, never over an existing email, never
// one another member uses - and the admin can turn it off.
TEST_F(DiscordFakeTest, DiscordSignInFillsMissingEmail) {
    auto start = GET("/auth/login");
    EXPECT_NE(start.location.find("scope=identify%20email"), std::string::npos) << start.location;
    auto p = start.headers.find("oauth_state=");
    ASSERT_NE(p, std::string::npos);
    std::string nonce = start.headers.substr(p + 12, start.headers.find(';', p) - p - 12);
    auto sign_in = [&](const std::string& id, const std::string& email, bool verified) {
        fake->oauth_user_id = id; fake->oauth_email = email; fake->oauth_verified = verified;
        return http("GET", "/auth/callback?code=abc&state=" + nonce, "", "", false, "", false, {"Cookie: oauth_state=" + nonce});
    };
    auto email_of = [&](const std::string& id) { return member_repo->find_by_discord_id(id)->email; };
    fake->add_member("100000000000000031", "ann", {MEMBER_ROLE});
    fake->add_member("100000000000000032", "bob", {MEMBER_ROLE});

    sign_in("100000000000000031", "Ann@Example.org", false);               // unverified: not used
    EXPECT_EQ(email_of("100000000000000031"), "");
    sign_in("100000000000000031", "Ann@Example.org", true);                // verified: filled in
    EXPECT_EQ(email_of("100000000000000031"), "ann@example.org");
    sign_in("100000000000000031", "new@example.org", true);                // already has one: kept
    EXPECT_EQ(email_of("100000000000000031"), "ann@example.org");
    sign_in("100000000000000032", "ann@example.org", true);                // someone else's: skipped
    EXPECT_EQ(email_of("100000000000000032"), "");
    settings_repo->set("auth_discord_email", "0");                          // switched off
    sign_in("100000000000000032", "bob@example.org", true);
    EXPECT_EQ(email_of("100000000000000032"), "");
    settings_repo->set("auth_discord_email", "1");
    sign_in("100000000000000032", "bob@example.org", true);
    EXPECT_EQ(email_of("100000000000000032"), "bob@example.org");
}
