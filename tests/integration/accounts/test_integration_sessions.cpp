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

// Sessions last 30 days from their last use: a session that's used is
// extended (at most once a day) and its cookie re-sent with the new lifetime.
TEST_F(IntegrationTest, SessionsSlideWhileUsed) {
    std::string tok = session_store->create(regular_member_id, "member", "Reg", 24 * 10);
    auto expiry = [&] {
        auto st = db->prepare("SELECT expires_at FROM sessions WHERE token=?");
        st.bind(1, sha256_hex(tok));
        return st.step() ? st.col_text(0) : std::string();
    };
    std::string before = expiry();
    auto r = GET("/dashboard", tok);
    EXPECT_EQ(r.code, 200);
    std::string after = expiry();
    EXPECT_GT(after, before);
    {   // ~30 days out (stored in UTC)
        std::time_t t = std::time(nullptr) + 29 * 86400;
        std::tm g{}; gmtime_r(&t, &g);
        char b[32]; std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &g);
        EXPECT_GT(after, std::string(b));
    }
    EXPECT_NE(r.headers.find("session=" + tok + "; HttpOnly; Path=/; Max-Age=2592000"), std::string::npos) << r.headers;
    // Used again the same day: not extended again, no new cookie
    auto again = GET("/dashboard", tok);
    EXPECT_EQ(expiry(), after);
    EXPECT_EQ(again.headers.find("session=" + tok), std::string::npos);
    // A new sign-in lasts 30 days
    EXPECT_EQ(SessionStore::kSessionHours, 720);
}
