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

    auto f = POST("/setup/features", "dues=1&perks=1", admin_token);      // everything else off
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
