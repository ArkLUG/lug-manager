// Public upcoming-shows page.
#include "integration_test_base.hpp"

namespace {
LugEvent show(const std::string& title, const std::string& start, const std::string& end) {
    LugEvent e;
    e.title = title; e.start_time = start; e.end_time = end;
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    return e;
}
}

TEST_F(IntegrationTest, ShowsOffByDefault) {
    EXPECT_EQ(GET("/shows").code, 404);
    EXPECT_EQ(GET("/shows.json").code, 404);
    EXPECT_EQ(GET("/settings/public-shows", chapter_lead_token).code, 403);
    EXPECT_EQ(POST("/settings/public-shows", "enabled=1", chapter_lead_token).code, 403);
    expect_contains(GET("/settings/public-shows", admin_token), "Publish the page");
}

TEST_F(IntegrationTest, ShowsListsPublicUpcomingOnly) {
    auto pub = show("Brickfair Booth", "2099-06-06T10:00:00", "2099-06-07T16:00:00");
    pub.location = "Expo Center"; pub.entrance_fee = "$5"; pub.description = "Come see **trains**";
    event_svc->create(pub);
    auto priv = show("Secret Build Night", "2099-06-10T18:00:00", "2099-06-10T21:00:00");
    priv.is_private = true;
    event_svc->create(priv);
    auto past = show("Old Show", "2001-01-01T10:00:00", "2001-01-01T16:00:00");
    event_svc->create(past);
    auto cancelled = show("Called Off", "2099-07-01T10:00:00", "2099-07-01T16:00:00");
    cancelled.status = "cancelled";
    event_svc->create(cancelled);
    auto tentative = show("Maybe Show", "2099-08-01T10:00:00", "2099-08-01T16:00:00");
    tentative.status = "tentative";
    event_svc->create(tentative);

    auto saved = POST("/settings/public-shows", "enabled=1&title=Arkansas+LUG+shows&intro=Hi+%3Cb%3E", admin_token);
    EXPECT_EQ(saved.code, 200);
    expect_contains(saved, "Saved.");

    auto r = GET("/shows");   // no login
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Arkansas LUG shows");
    expect_contains(r, "Hi &lt;b&gt;");
    expect_contains(r, "Brickfair Booth");
    expect_contains(r, "Sat, Jun 6 - Sun, Jun 7, 2099");
    expect_contains(r, "Expo Center");
    expect_contains(r, "Admission: $5");
    expect_contains(r, "<strong>trains</strong>");
    expect_contains(r, "Date to be confirmed");
    expect_not_contains(r, "Secret Build Night");
    expect_not_contains(r, "Old Show");
    expect_not_contains(r, "Called Off");
    EXPECT_NE(r.headers.find("frame-ancestors 'none'"), std::string::npos);

    auto e = GET("/shows?embed=1");
    EXPECT_EQ(e.code, 200);
    EXPECT_NE(e.headers.find("frame-ancestors *"), std::string::npos);
    expect_not_contains(e, "Arkansas LUG shows</h1>");

    auto j = GET("/shows.json");
    EXPECT_EQ(j.code, 200);
    EXPECT_NE(j.headers.find("Access-Control-Allow-Origin: *"), std::string::npos);
    auto parsed = crow::json::load(j.body);
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0]["title"].s(), "Brickfair Booth");
    EXPECT_TRUE(parsed[1]["tentative"].b());

    POST("/settings/public-shows", "title=x", admin_token);   // unticked -> off
    EXPECT_EQ(GET("/shows").code, 404);
}
