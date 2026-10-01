// Database backups.
#include "integration_test_base.hpp"

TEST_F(IntegrationTest, BackupCreateListDownload) {
    EXPECT_EQ(POST("/settings/backups", "", chapter_lead_token).code, 403);
    auto r = POST("/settings/backups", "", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "created");
    size_t p = r.body.find("/settings/backups/lug-");
    ASSERT_NE(p, std::string::npos);
    std::string url = r.body.substr(p, std::string("/settings/backups/lug-20260101-000000.db").size());
    auto dl = GET(url, admin_token);
    EXPECT_EQ(dl.code, 200);
    EXPECT_EQ(dl.body.substr(0, 15), "SQLite format 3");
    EXPECT_EQ(GET(url, chapter_lead_token).code, 403);
}

TEST_F(IntegrationTest, BackupDownloadRejectsOtherFiles) {
    EXPECT_EQ(GET("/settings/backups/..%2Flug.db", admin_token).code, 404);
    EXPECT_EQ(GET("/settings/backups/lug.db", admin_token).code, 404);
}
