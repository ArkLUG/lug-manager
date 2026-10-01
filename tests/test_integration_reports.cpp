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

TEST_F(IntegrationTest, AnnualReportGrowthAndRetention) {
    auto meeting = [&](const std::string& title, const std::string& start, const std::string& loc, bool virt = false) {
        auto st = db->prepare("INSERT INTO meetings (title, start_time, end_time, location, status, scope, ical_uid, is_virtual) "
                              "VALUES (?, ?, ?, ?, 'completed', 'lug_wide', ?, ?) RETURNING id");
        st.bind(1, title); st.bind(2, start); st.bind(3, start); st.bind(4, loc); st.bind(5, title + "-uid");
        st.bind(6, static_cast<int64_t>(virt));
        st.step();
        return st.col_int(0);
    };
    auto attend = [&](int64_t member, int64_t mid) {
        auto st = db->prepare("INSERT INTO attendance (member_id, entity_type, entity_id) VALUES (?, 'meeting', ?)");
        st.bind(1, member); st.bind(2, mid); st.step();
    };
    int64_t m1 = meeting("Old Night", "2030-04-01T19:00:00", "Library");
    attend(regular_member_id, m1); attend(chapter_lead_member_id, m1);
    int64_t m2 = meeting("New Night", "2031-04-01T19:00:00", "Library");
    attend(regular_member_id, m2); attend(event_manager_member_id, m2);
    meeting("Online Night", "2031-05-01T19:00:00", "Discord", true);
    {
        auto e = db->prepare("INSERT INTO lug_events (title, start_time, end_time, location, status, scope, ical_uid, public_adults) "
                             "VALUES ('Expo', '2031-06-01T09:00:00', '2031-06-01T17:00:00', 'Expo Center', 'confirmed', 'lug_wide', 'expo-uid', 40)");
        e.step();
        auto u = db->prepare("UPDATE members SET created_at='2031-03-05 12:00:00' WHERE id=?");
        u.bind(1, event_manager_member_id); u.step();
    }
    auto r = GET("/reports/annual?year=2031", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Growth &amp; retention");
    expect_contains(r, ">1</div><div class=\"text-xs text-gray-500\">came back from 2030 (50%)");
    expect_contains(r, ">1</div><div class=\"text-xs text-gray-500\">active for the first time");
    expect_contains(r, ">1</div><div class=\"text-xs text-gray-500\">active in 2030, not this year");
    // Five-year trend: 2031 has 2 meetings (one virtual), 2 check-ins -> 1.0 per meeting, 40 visitors
    expect_contains(r, "<td class=\"py-1\">2031</td><td class=\"text-right\">2</td><td class=\"text-right\">1</td><td class=\"text-right\">2</td><td class=\"text-right\">1.0</td><td class=\"text-right\">40</td>");
    expect_contains(r, "<td class=\"py-1\">2030</td><td class=\"text-right\">2</td>");
    // Venues: in-person only, busiest first
    auto expo = r.body.find(">Expo Center<"), lib = r.body.find(">Library<");
    ASSERT_NE(expo, std::string::npos);
    ASSERT_NE(lib, std::string::npos);
    EXPECT_LT(expo, lib);
    expect_not_contains(r, ">Discord<");
}
