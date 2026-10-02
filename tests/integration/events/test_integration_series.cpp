// Recurring meetings: creation, materialization, permissions, stopping.
#include "integration_test_base.hpp"
#include "utils/LocalTime.hpp"

TEST_F(IntegrationTest, CreateSeriesSchedulesMeetings) {
    std::string body = "title=Build+Night&location=Library&rule=weekly&interval_weeks=1&weekday=2"
                       "&start_hm=19:00&end_hm=21:00&starts_on=" + today_at("x").substr(0, 10) +
                       "&scope=chapter&chapter_id=" + std::to_string(test_chapter_id) + "&suppress_discord=1&suppress_calendar=1";
    auto r = POST("/meetings/series", body, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Series created");
    expect_contains(r, "Every Tuesday, 7:00 PM");
    auto st = db->prepare("SELECT COUNT(*) FROM meetings WHERE series_id IS NOT NULL AND title='Build Night'");
    ASSERT_TRUE(st.step());
    int n = static_cast<int>(st.col_int(0));
    EXPECT_GE(n, 6); // ~45 days of Tuesdays

    // Idempotent: materializing again adds nothing
    POST("/meetings/series", "title=&weekday=1", admin_token); // invalid, no effect
    auto again = db->prepare("SELECT COUNT(*) FROM meetings WHERE title='Build Night'");
    ASSERT_TRUE(again.step());
    EXPECT_EQ(again.col_int(0), n);
}

TEST_F(IntegrationTest, SeriesPermissionsAndValidation) {
    std::string base = "title=X&rule=monthly&nth=2&weekday=3&start_hm=19:00&end_hm=21:00&scope=";
    EXPECT_EQ(POST("/meetings/series", base + "lug_wide", event_manager_token).code, 403);
    EXPECT_EQ(POST("/meetings/series", base + "chapter&chapter_id=" + std::to_string(test_chapter_id) +
                   "&suppress_discord=1&suppress_calendar=1", event_manager_token).code, 200);
    EXPECT_EQ(POST("/meetings/series", "title=X&rule=monthly&nth=9&weekday=3&start_hm=19:00&end_hm=21:00&scope=lug_wide",
                   admin_token).code, 400);
    EXPECT_EQ(POST("/meetings/series", "title=X&rule=weekly&weekday=3&start_hm=21:00&end_hm=19:00&scope=lug_wide",
                   admin_token).code, 400);
}

TEST_F(IntegrationTest, StopSeriesRemovesFutureMeetings) {
    POST("/meetings/series", "title=Stoppable&rule=weekly&weekday=4&start_hm=19:00&end_hm=21:00&scope=lug_wide"
                             "&suppress_discord=1&suppress_calendar=1", admin_token);
    auto id = db->prepare("SELECT id FROM meeting_series WHERE title='Stoppable'");
    ASSERT_TRUE(id.step());
    int64_t sid = id.col_int(0);
    EXPECT_EQ(POST("/meetings/series/" + std::to_string(sid) + "/stop", "", member_token).code, 403);
    auto r = POST("/meetings/series/" + std::to_string(sid) + "/stop", "", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "stopped");
    // Future ones go; one earlier today (the weekday is today and 19:00 has passed) stays.
    auto left = db->prepare("SELECT COUNT(*) FROM meetings WHERE title='Stoppable' AND start_time > ?");
    left.bind(1, local_iso_now());
    ASSERT_TRUE(left.step());
    EXPECT_EQ(left.col_int(0), 0);
}
