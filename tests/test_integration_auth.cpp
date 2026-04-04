#include "integration_test_base.hpp"

TEST_F(IntegrationTest, DashboardLoads) {
    auto r = GET("/dashboard", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Edit Profile");
    expect_contains(r, "calendar.ics");
    expect_contains(r, "Members");
    expect_contains(r, "Meetings");
    expect_contains(r, "Events");
}

TEST_F(IntegrationTest, DashboardHtmxPartial) {
    auto r = GET_HTMX("/dashboard", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Edit Profile");
    // Partial should NOT have full layout
    expect_not_contains(r, "<html");
}

// ═══════════════════════════════════════════════════════════════════════════
// Calendar (public)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, CalendarIcsPublic) {
    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "BEGIN:VCALENDAR");
    expect_contains(r, "END:VCALENDAR");
}

// ═══════════════════════════════════════════════════════════════════════════
// Members
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, RootUnauthenticatedRedirectsToLogin) {
    auto r = GET("/");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
}

TEST_F(IntegrationTest, RootAuthenticatedRedirectsToDashboard) {
    auto r = GET("/", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/dashboard"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Members — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, UnauthenticatedRedirectsToLogin) {
    auto r = GET("/dashboard");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
}

TEST_F(IntegrationTest, UnauthenticatedHtmxReturns401) {
    auto r = GET_HTMX("/meetings");
    EXPECT_EQ(r.code, 401);
    expect_contains(r, "Session expired");
}

TEST_F(IntegrationTest, LoginPageLoads) {
    auto r = GET("/login");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Login");
    expect_contains(r, "Discord");
}

TEST_F(IntegrationTest, LogoutWorks) {
    auto r = GET("/auth/logout", admin_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login"), std::string::npos);
}

TEST_F(IntegrationTest, AdminCanAccessSettings) {
    auto r = GET("/settings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Settings");
}

TEST_F(IntegrationTest, MemberCannotAccessSettings) {
    auto r = GET("/settings", member_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307); // redirects to dashboard
}
