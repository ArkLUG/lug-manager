// First-run setup: first admin via the one-time token, then the checklist.
#include "integration_test_base.hpp"
#include "routes/SetupRoutes.hpp"
#include "services/Features.hpp"

TEST_F(IntegrationTest, SetupFirstAdminWithToken) {
    EXPECT_EQ(ensure_setup_token(*db), "");                  // fixture already has an admin
    {
        auto u = db->prepare("UPDATE members SET role='member' WHERE role='admin'");
        u.step();
    }
    std::string token = ensure_setup_token(*db);
    ASSERT_EQ(token.size(), 32u);
    EXPECT_EQ(GET("/setup").code, 404);                       // no token: nothing to see
    EXPECT_EQ(GET("/setup?token=wrong").code, 404);
    auto form = GET("/setup?token=" + token);
    EXPECT_EQ(form.code, 200);
    expect_contains(form, "Create the first admin");

    EXPECT_EQ(POST("/setup/admin", "token=" + token + "&first_name=Ann").code, 400);    // needs Discord id or email
    EXPECT_EQ(POST("/setup/admin", "token=" + token + "&first_name=Ann&discord_user_id=12ab").code, 400);
    auto ok = POST("/setup/admin", "token=" + token + "&first_name=Ann&last_name=Admin&discord_user_id=123456789012345678");
    EXPECT_EQ(ok.code, 200);
    expect_contains(ok, "Sign in with Discord");
    auto m = member_repo->find_by_discord_id("123456789012345678");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->role, "admin");
    EXPECT_EQ(member_repo->get_role_source(m->id), "manual");
    // Single use: the token is gone and can't make a second admin
    EXPECT_EQ(setup_token(), "");
    EXPECT_EQ(POST("/setup/admin", "token=" + token + "&first_name=Eve&discord_user_id=999").code, 404);
    EXPECT_FALSE(member_repo->find_by_discord_id("999").has_value());
    EXPECT_NE(GET("/setup?token=" + token).code, 200);         // now needs an admin login
}

TEST_F(IntegrationTest, SetupChecklistForAdmins) {
    EXPECT_EQ(GET("/setup", member_token).code, 403);
    expect_contains(GET("/dashboard", admin_token), "Finish setting up your LUG");
    expect_not_contains(GET("/dashboard", member_token), "Finish setting up your LUG");
    auto page = GET("/setup", admin_token);
    expect_contains(page, "0 of 4 done");

    EXPECT_EQ(POST("/setup/basics", "lug_name=", admin_token).code, 400);
    auto b = POST("/setup/basics", "lug_name=Test+LUG&lug_timezone=America%2FDenver", admin_token);
    expect_contains(b, "1 of 4 done");
    EXPECT_EQ(settings_repo->get("lug_timezone", ""), "America/Denver");
    expect_contains(GET("/dashboard", admin_token), ">Test LUG</div>");     // sidebar shows the name

    auto f = POST("/setup/features", "dues=1&perks=1&discord=1", admin_token);      // everything else off
    expect_contains(f, "2 of 4 done");
    EXPECT_FALSE(Features::on("chapters"));
    EXPECT_TRUE(Features::on("dues"));

    EXPECT_EQ(POST("/setup/discord", "discord_guild_id=abc", admin_token).code, 400);
    auto d = POST("/setup/discord", "discord_guild_id=123456789012345678&discord_announcements_channel_id=223456789012345678", admin_token);
    expect_contains(d, "3 of 4 done");
    EXPECT_EQ(settings_repo->get("discord_guild_id", ""), "123456789012345678");

    role_mapping_repo->upsert("323456789012345678", "Admins", "admin");
    expect_contains(GET("/setup", admin_token), "4 of 4 done");

    EXPECT_EQ(POST("/setup/finish", "", member_token).code, 403);
    auto fin = POST("/setup/finish", "", admin_token);
    EXPECT_NE(fin.headers.find("HX-Redirect: /dashboard"), std::string::npos);
    expect_not_contains(GET("/dashboard", admin_token), "Finish setting up your LUG");
}

#include "services/MemberMerge.hpp"
TEST_F(IntegrationTest, SetupFanCoLabRecognitionAndAmbassador) {
    // Neutral default name until the LUG names itself
    expect_contains(GET("/dashboard", member_token), ">LEGO fan community</div>");
    EXPECT_EQ(POST("/setup/fancolab", "fan_colab_recognized=1", member_token).code, 403);
    EXPECT_EQ(POST("/setup/fancolab", "fan_colab_recognized=1&community_ambassador_id=999999", admin_token).code, 400);

    LugEvent e;
    e.title = "CoLab Show"; e.start_time = "2030-05-01T09:00:00"; e.end_time = "2030-05-01T16:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    std::string report = "/events/" + std::to_string(ev.id) + "/report";
    expect_not_contains(GET(report, admin_token), "Recognized LEGO");

    settings_repo->set("lug_name", "Test LUG");
    auto r = POST("/setup/fancolab", "fan_colab_recognized=1&community_ambassador_id=" + std::to_string(regular_member_id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "&#10003; 5. LEGO Fan CoLab");
    expect_contains(GET(report, admin_token), "Test LUG - a Recognized LEGO® Fan Community (LEGO Fan CoLab) · Community Ambassador: Regular U.");
    expect_contains(GET("/reports/annual?year=2030", admin_token), "Community Ambassador: Regular U.");
    Features::set("public_shows", true);
    expect_contains(GET("/shows"), "A Recognized LEGO® Fan Community");

    // The ambassador follows a member merge
    Member dup; dup.first_name = "Reg"; dup.last_name = "Dup"; dup.display_name = "Reg D."; dup.role = "member";
    int64_t keep = member_repo->create(dup).id;
    MemberMerge(*db).merge(keep, regular_member_id);
    EXPECT_EQ(settings_repo->get("community_ambassador_id", ""), std::to_string(keep));
    expect_contains(GET(report, admin_token), "Community Ambassador: Reg D.");

    // Not recognized -> nothing shown
    POST("/setup/fancolab", "community_ambassador_id=", admin_token);
    expect_not_contains(GET(report, admin_token), "Recognized LEGO");
    expect_not_contains(GET("/shows"), "Recognized LEGO");
}

TEST_F(IntegrationTest, SetupFirstAdminWithPassword) {
    {
        auto u = db->prepare("UPDATE members SET role='member' WHERE role='admin'");
        u.step();
    }
    std::string token = ensure_setup_token(*db);
    // A password needs an email, and must be good enough
    EXPECT_EQ(POST("/setup/admin", "token=" + token + "&first_name=Ann&discord_user_id=123456789012345678&password=long+enough+pw&confirm=long+enough+pw").code, 400);
    EXPECT_EQ(POST("/setup/admin", "token=" + token + "&first_name=Ann&email=ann%40example.org&password=short&confirm=short").code, 400);
    EXPECT_EQ(POST("/setup/admin", "token=" + token + "&first_name=Ann&email=ann%40example.org&password=long+enough+pw&confirm=other+one+here").code, 400);
    auto ok = POST("/setup/admin", "token=" + token + "&first_name=Ann&last_name=Admin&email=ann%40example.org&password=long+enough+pw&confirm=long+enough+pw");
    EXPECT_EQ(ok.code, 200);
    expect_contains(ok, "ann@example.org</strong> and your password");
    // Signs in with no Discord and no email server
    auto in = POST("/auth/password", "email=ann%40example.org&password=long+enough+pw");
    EXPECT_NE(in.location.find("/dashboard"), std::string::npos);
    EXPECT_NE(in.headers.find("session="), std::string::npos);
}

TEST_F(IntegrationTest, SetupChecklistWithoutDiscord) {
    Features::set("discord", false);
    auto page = GET("/setup", admin_token);
    expect_contains(page, "3. Discord");
    expect_contains(page, "Discord is switched off");
    expect_not_contains(page, "Server ID");
    expect_contains(page, "1 of 4 done");                       // step 3 counts as done
    Member a; a.first_name = "Ann"; a.display_name = "Ann A."; a.email = "a@example.org"; a.role = "member";
    Member b; b.first_name = "Bob"; b.display_name = "Bob B."; b.email = "b@example.org"; b.role = "member";
    member_repo->create(a); member_repo->create(b);
    expect_contains(GET("/setup", admin_token), "2 of 4 done");
    Features::set("discord", true);
}
