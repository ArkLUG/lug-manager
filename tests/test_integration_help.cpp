#include "integration_test_base.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Help / Onboarding — verify role-based content rendering
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, HelpPageUnauthenticated) {
    auto r = GET("/help");
    // Should redirect to login
    EXPECT_NE(r.code, 200);
}

TEST_F(IntegrationTest, HelpPageMember) {
    auto r = GET("/help", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Welcome to LUG Manager");
    expect_contains(r, "Getting Started");
    expect_contains(r, "Quick Reference");
    // Member should see role label "Member"
    expect_contains(r, "Member");
    // Member should NOT see admin or chapter lead guides
    expect_not_contains(r, "Admin Guide");
    expect_not_contains(r, "Chapter Lead Guide");
}

TEST_F(IntegrationTest, HelpPageAdmin) {
    auto r = GET("/help", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Welcome to LUG Manager");
    expect_contains(r, "Admin Guide");
    expect_contains(r, "Event Manager Guide");
    // Admin role label
    expect_contains(r, "Admin");
}

TEST_F(IntegrationTest, HelpPageChapterLead) {
    auto r = GET("/help", chapter_lead_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Chapter Lead Guide");
    expect_contains(r, "Event Manager Guide");
    expect_contains(r, "Chapter Lead");
}

TEST_F(IntegrationTest, HelpPageEventManager) {
    auto r = GET("/help", event_manager_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Event Manager Guide");
    expect_contains(r, "Event Manager");
    // Event manager should NOT see chapter lead or admin guides
    expect_not_contains(r, "Chapter Lead Guide");
    expect_not_contains(r, "Admin Guide");
}

TEST_F(IntegrationTest, HelpPageHtmx) {
    auto r = GET_HTMX("/help", member_token);
    EXPECT_EQ(r.code, 200);
    // HTMX returns partial content (no full layout)
    expect_contains(r, "Welcome to LUG Manager");
    expect_not_contains(r, "<!DOCTYPE html>");
}

TEST_F(IntegrationTest, HelpPageFullLayout) {
    auto r = GET("/help", member_token);
    EXPECT_EQ(r.code, 200);
    // Full page includes layout wrapper
    expect_contains(r, "Help &amp; Getting Started");
}

TEST_F(IntegrationTest, HelpPageQuickReferenceTable) {
    auto r = GET("/help", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Quick Reference");
    expect_contains(r, "View meetings/events");
    expect_contains(r, "Edit own profile");
}

TEST_F(IntegrationTest, HelpPageInteractiveTour) {
    auto r = GET("/help", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Interactive Tour");
    expect_contains(r, "startTour");
}
