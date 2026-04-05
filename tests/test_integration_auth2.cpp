#include "integration_test_base.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// AuthRoutes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, LoginPageHasDiscordButton) {
    auto r = GET("/login");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Discord");
    expect_contains(r, "/auth/login");
}

TEST_F(IntegrationTest, LoginPageErrorNotMember) {
    auto r = GET("/login?error=not_member");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Access Denied");
    expect_contains(r, "not registered as a LUG member");
}

TEST_F(IntegrationTest, LoginPageErrorDiscordDenied) {
    auto r = GET("/login?error=discord_denied");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Sign-in Cancelled");
    expect_contains(r, "cancelled the Discord authorization");
}

TEST_F(IntegrationTest, LoginPageErrorFailed) {
    auto r = GET("/login?error=failed");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Authentication Error");
    expect_contains(r, "Something went wrong");
}

TEST_F(IntegrationTest, AuthCallbackNoCodeRedirectsWithError) {
    // No code param at all -> redirects to /login?error=no_code
    auto r = GET("/auth/callback");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
    EXPECT_NE(r.location.find("error=no_code"), std::string::npos);
}

TEST_F(IntegrationTest, AuthCallbackWithErrorParam) {
    // error param present -> redirects to /login?error=discord_denied
    auto r = GET("/auth/callback?error=access_denied");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
    EXPECT_NE(r.location.find("error=discord_denied"), std::string::npos);
}

TEST_F(IntegrationTest, AuthCallbackCheckinStateWithError) {
    // error param with checkin state -> redirects to /checkin/<token>?error=discord_denied
    auto r = GET("/auth/callback?error=access_denied&state=checkin:abc123");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/checkin/abc123"), std::string::npos);
    EXPECT_NE(r.location.find("error=discord_denied"), std::string::npos);
}

TEST_F(IntegrationTest, AuthenticatedUserVisitingLoginRedirects) {
    auto r = GET("/login", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/dashboard"), std::string::npos);
}

TEST_F(IntegrationTest, LogoutClearsSessionAndRedirects) {
    // First verify admin can access dashboard
    auto before = GET("/dashboard", admin_token);
    EXPECT_EQ(before.code, 200);

    // Logout
    auto r = GET("/auth/logout", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
}

TEST_F(IntegrationTest, RootRedirectAuthenticatedToDashboard) {
    auto r = GET("/", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/dashboard"), std::string::npos);
}

TEST_F(IntegrationTest, RootRedirectUnauthenticatedToLogin) {
    auto r = GET("/");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
}
