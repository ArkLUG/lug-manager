// Signed-in devices.
#include "integration_test_base.hpp"

TEST_F(IntegrationTest, SignOutOtherDevices) {
    std::string other = session_store->create(regular_member_id, "member", "Reg", 24, "Mozilla/5.0 (X11; Linux) Firefox/130.0");
    auto list = GET("/account/sessions", member_token);
    EXPECT_EQ(list.code, 200);
    expect_contains(list, "Firefox on Linux");
    expect_contains(list, "this device");

    auto r = POST("/account/sessions/revoke-others", "", member_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(session_store->find(other).has_value());
    EXPECT_TRUE(session_store->find(member_token).has_value());
}

TEST_F(IntegrationTest, AdminSignsMemberOutEverywhere) {
    EXPECT_EQ(POST("/members/" + std::to_string(regular_member_id) + "/sessions/revoke", "", chapter_lead_token).code, 403);
    EXPECT_EQ(POST("/members/" + std::to_string(regular_member_id) + "/sessions/revoke", "", admin_token).code, 200);
    EXPECT_FALSE(session_store->find(member_token).has_value());
}
