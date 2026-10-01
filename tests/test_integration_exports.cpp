// CSV exports: access control and content.
#include "integration_test_base.hpp"

TEST_F(IntegrationTest, MembersCsvForChapterLeadsOnly) {
    EXPECT_EQ(GET("/members.csv", member_token).code, 403);
    auto r = GET("/members.csv", chapter_lead_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Display name");
    expect_contains(r, "Lead U.");
    EXPECT_NE(r.headers.find("text/csv"), std::string::npos);
}

TEST_F(IntegrationTest, AttendanceAndAuditCsvAdminOnly) {
    EXPECT_EQ(GET("/attendance/overview.csv?year=2026", chapter_lead_token).code, 403);
    auto a = GET("/attendance/overview.csv?year=2026", admin_token);
    EXPECT_EQ(a.code, 200);
    expect_contains(a, "Perk tier (2026)");

    EXPECT_EQ(GET("/audit.csv", chapter_lead_token).code, 403);
    auto l = GET("/audit.csv", admin_token);
    EXPECT_EQ(l.code, 200);
    expect_contains(l, "export.attendance"); // the export itself is audited
}
