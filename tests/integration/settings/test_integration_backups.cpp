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

#include "utils/web/ZipWriter.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
std::string slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream b;
    b << in.rdbuf();
    return b.str();
}
uint32_t le32(const std::string& s, size_t at) {
    return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 |
           uint32_t(uint8_t(s[at + 3])) << 24;
}
}

TEST(ZipWriter, WritesReadableStoredArchive) {
    auto dir = std::filesystem::temp_directory_path() / ("zipw-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    { std::ofstream(dir / "a.txt") << "hello"; }
    {
        ZipWriter z((dir / "t.zip").string());
        z.add_file("files/a.txt", (dir / "a.txt").string());
        z.add_bytes("b.txt", "world!");
        z.finish();
    }
    std::string zip = slurp((dir / "t.zip").string());
    ASSERT_GT(zip.size(), 22u);
    EXPECT_EQ(zip.substr(0, 4), std::string("PK\x03\x04", 4));
    size_t eocd = zip.size() - 22;
    EXPECT_EQ(zip.substr(eocd, 4), std::string("PK\x05\x06", 4));
    EXPECT_EQ(uint8_t(zip[eocd + 10]), 2);                 // two entries
    EXPECT_EQ(le32(zip, 14), crc32(0, reinterpret_cast<const Bytef*>("hello"), 5));
    EXPECT_NE(zip.find("files/a.txthello"), std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_F(IntegrationTest, BackupMirrorsUploadsAndOffersArchive) {
    LugEvent e;
    e.title = "Backup Show"; e.start_time = "2099-07-01T09:00:00"; e.end_time = "2099-07-01T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    EXPECT_EQ(GET("/settings/backups/photos.zip", admin_token).code, 404);   // nothing yet
    auto up = POST_FILE("/events/" + std::to_string(ev.id) + "/photos", "photo", "a.png", kTinyPng, member_token);
    ASSERT_EQ(up.code, 200);
    auto p = up.body.find("/uploads/");
    std::string name = up.body.substr(p + 9, up.body.find('"', p) - p - 9);

    auto page = POST("/settings/backups", "", admin_token);
    expect_contains(page, "1 file,");
    std::string mirrored = data_dir + "/backups/uploads/" + name;
    EXPECT_TRUE(std::filesystem::exists(mirrored));

    EXPECT_EQ(GET("/settings/backups/photos.zip", chapter_lead_token).code, 403);
    auto zip = GET("/settings/backups/photos.zip", admin_token);
    EXPECT_EQ(zip.code, 200);
    EXPECT_EQ(zip.body.substr(0, 4), std::string("PK\x03\x04", 4));
    EXPECT_NE(zip.body.find("uploads/" + name), std::string::npos);

    // Deleting the photo keeps the backup copy for the retention period
    auto q = db->prepare("SELECT id FROM event_photos WHERE event_id=?");
    q.bind(1, ev.id);
    ASSERT_TRUE(q.step());
    std::string del = "/events/" + std::to_string(ev.id) + "/photos/" + std::to_string(q.col_int(0)) + "/delete";
    q.reset();
    ASSERT_EQ(POST(del, "", member_token).code, 200);
    POST("/settings/backups", "", admin_token);
    EXPECT_TRUE(std::filesystem::exists(mirrored));
    EXPECT_NE(slurp(data_dir + "/backups/uploads/.deleted").find(name), std::string::npos);
    // ...and drops it once that period is over
    BackupService(*db, data_dir).mirror_uploads(-1);
    EXPECT_FALSE(std::filesystem::exists(mirrored));
}
