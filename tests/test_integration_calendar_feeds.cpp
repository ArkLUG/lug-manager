// Chapter and personal calendar feeds.
#include "integration_test_base.hpp"

namespace {
Meeting mk(const std::string& title, const std::string& scope, int64_t chapter, bool priv = false) {
    Meeting m;
    m.title = title;
    m.start_time = "2099-03-01T19:00:00";
    m.end_time = "2099-03-01T21:00:00";
    m.scope = scope;
    m.chapter_id = chapter;
    m.is_private = priv;
    m.suppress_discord = true;
    m.suppress_calendar = true;
    return m;
}
}

TEST_F(IntegrationTest, ChapterFeedFiltersItems) {
    Chapter other; other.name = "Other"; other.shorthand = "OT";
    auto oc = chapter_repo->create(other);
    meeting_svc->create(mk("Mine", "chapter", test_chapter_id));
    meeting_svc->create(mk("Theirs", "chapter", oc.id));
    meeting_svc->create(mk("Everyone", "lug_wide", 0));
    std::string url = "/calendar/chapter/" + std::to_string(test_chapter_id) + "/feed.ics";
    auto r = GET(url);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Mine");
    expect_contains(r, "Everyone");
    EXPECT_EQ(r.body.find("Theirs"), std::string::npos);
    auto only = GET(url + "?lug_wide=0");
    EXPECT_EQ(only.body.find("Everyone"), std::string::npos);
}

TEST_F(IntegrationTest, PersonalFeedShowsPrivateDetails) {
    meeting_svc->create(mk("Secret Planning", "lug_wide", 0, true));
    EXPECT_EQ(GET("/calendar.ics").body.find("Secret Planning"), std::string::npos);

    auto t = POST("/account/calendar-token", "", member_token);
    EXPECT_EQ(t.code, 200);
    size_t p = t.body.find("/calendar/me/");
    ASSERT_NE(p, std::string::npos);
    std::string path = t.body.substr(p, std::string("/calendar/me/").size() + 64 + std::string("/feed.ics").size());
    auto feed = GET(path);
    EXPECT_EQ(feed.code, 200);
    expect_contains(feed, "Secret Planning");

    // Regenerating invalidates the old link
    POST("/account/calendar-token", "", member_token);
    EXPECT_EQ(GET(path).code, 404);
    EXPECT_EQ(GET("/calendar/me/" + std::string(64, 'a') + "/feed.ics").code, 404);
}
