// Annual report and per-event (LAN-style) report.
#include "integration_test_base.hpp"

TEST_F(IntegrationTest, AnnualReportNumbers) {
    Meeting m;
    m.title = "Report Meeting";
    m.start_time = "2031-05-05T19:00:00";
    m.end_time = "2031-05-05T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);
    attendance_repo->check_in(regular_member_id, "meeting", mtg.id);
    attendance_repo->check_in(admin_member_id, "meeting", mtg.id);

    EXPECT_EQ(GET("/reports/annual?year=2031", chapter_lead_token).code, 403);
    auto r = GET("/reports/annual?year=2031", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "2031 in Numbers");
    expect_contains(r, "2 check-ins");
    expect_contains(r, "Most active members");
}

TEST_F(IntegrationTest, EventReportForManagers) {
    LugEvent e;
    e.title = "Report Show";
    e.start_time = "2031-06-01T09:00:00";
    e.end_time = "2031-06-02T17:00:00";
    e.scope = "lug_wide";
    e.status = "confirmed";
    e.suppress_discord = true;
    e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    event_repo->add_visitors(ev.id, "kids", 1);
    event_repo->add_visitors(ev.id, "adults", 1);
    std::string url = "/events/" + std::to_string(ev.id) + "/report";
    EXPECT_EQ(GET(url, member_token).code, 403);
    auto r = GET(url, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Report Show");
    expect_contains(r, "2 (1 kids, 0 teens, 1 adults)");
    expect_contains(r, "Member attendance by day");
}
