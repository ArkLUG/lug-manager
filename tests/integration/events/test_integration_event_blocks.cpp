// Shows' day-by-day hours (event_blocks): setup / public / teardown blocks.
#include "integration_test_base.hpp"

namespace {
LugEvent show(const std::string& title, const std::string& first, const std::string& last) {
    LugEvent e;
    e.title = title; e.start_time = first + "T00:00:00"; e.end_time = last + "T00:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    return e;
}
size_t count(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}
}

TEST_F(IntegrationTest, ShowHoursSetupAndTeardown) {
    auto ev = event_svc->create(show("Block Show", "2099-11-13", "2099-11-14"));
    const std::string base = "/events/" + std::to_string(ev.id) + "/blocks";
    expect_contains(GET(base, member_token), "No hours set");
    EXPECT_EQ(POST(base + "/fill", "from=09:00&to=17:00", member_token).code, 403);   // members can't

    // Same public hours both days; setup the evening before; teardown after closing Saturday
    auto r = POST(base + "/fill", "from=09:00&to=17:00", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(count(r.body, "Open to the public"), 2u + 1u);                // 2 blocks + the form option
    ASSERT_EQ(POST(base, "kind=setup&day=2099-11-12&from=16:00&to=20:00&label=Back+door", admin_token).code, 200);
    ASSERT_EQ(POST(base, "kind=teardown&day=2099-11-14&from=17:00&to=19:00", admin_token).code, 200);
    // Validation: public hours only on the show's dates; times the right way round; not too far out
    EXPECT_EQ(POST(base, "kind=public&day=2099-11-12&from=09:00&to=17:00", admin_token).code, 400);
    EXPECT_EQ(POST(base, "kind=setup&day=2099-11-12&from=20:00&to=16:00", admin_token).code, 400);
    EXPECT_EQ(POST(base, "kind=setup&day=2099-10-01&from=16:00&to=20:00", admin_token).code, 400);
    EXPECT_EQ(POST(base, "kind=party&day=2099-11-12&from=16:00&to=20:00", admin_token).code, 400);

    // The setup day is a check-in day too
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM event_days WHERE event_id=?", ev.id), 3);
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM event_days WHERE event_id=? AND day_date='2099-11-12'", ev.id), 1);

    // Public calendar: one timed entry per open day, no setup; members' own file has everything
    auto feed = GET("/calendar.ics");
    const std::string uid_base = ev.ical_uid.substr(0, ev.ical_uid.find('@'));
    EXPECT_EQ(count(feed.body, "SUMMARY:[Group-wide] Block Show"), 2u);
    EXPECT_NE(feed.body.find("DTSTART;TZID="), std::string::npos);
    EXPECT_NE(feed.body.find("T090000"), std::string::npos);
    EXPECT_EQ(feed.body.find("[Setup]"), std::string::npos);
    EXPECT_NE(feed.body.find("UID:" + uid_base + "-b"), std::string::npos);
    auto mine = GET("/events/" + std::to_string(ev.id) + "/calendar.ics", member_token);
    expect_contains(mine, "SUMMARY:[Setup] Block Show");
    expect_contains(mine, "SUMMARY:[Teardown] Block Show");
    expect_contains(mine, "Back door");

    // Schedule: hours in the list; the setup day labelled in the calendar
    expect_contains(GET("/schedule?when=all&q=Block", member_token), "9:00 AM – 5:00 PM");
    auto cal = GET("/schedule?view=calendar&month=2099-11", member_token);
    expect_contains(cal, "Setup: Block Show");

    // Public shows page: hours day by day
    Features::set("public_shows", true);
    auto pub = GET("/shows");
    expect_contains(pub, "Fri, Nov 13: 9:00 AM - 5:00 PM");
    expect_contains(pub, "Sat, Nov 14: 9:00 AM - 5:00 PM");
    expect_not_contains(pub, "4:00 PM - 8:00 PM");                        // setup isn't public

    // Removing the setup block drops its day again
    const int64_t setup = query_int(*db, "SELECT id FROM event_blocks WHERE event_id=? AND kind='setup'", ev.id);
    EXPECT_EQ(POST(base + "/" + std::to_string(setup) + "/delete", "", admin_token).code, 200);
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM event_days WHERE event_id=?", ev.id), 2);
    EXPECT_GE(query_int(*db, "SELECT COUNT(*) FROM audit_log WHERE action LIKE 'event.block_%'"), 4);
}

TEST_F(IntegrationTest, ShowWithoutHoursStaysAllDay) {
    auto ev = event_svc->create(show("Plain Show", "2099-12-05", "2099-12-05"));
    auto feed = GET("/calendar.ics");
    expect_contains(feed, "DTSTART;VALUE=DATE:20991205");
    Features::set("public_shows", true);
    auto pub = GET("/shows");
    expect_contains(pub, "Plain Show");
    expect_not_contains(pub, "12:00 AM");                                  // dates only, no midnight times
    (void)ev;
}
