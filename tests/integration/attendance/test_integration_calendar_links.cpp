// Calendar connections: "Add to calendar" links, single-item .ics files and
// the subscribe blocks (Schedule, dashboard, chapter page, My Account).
#include "integration_test_base.hpp"
#include "utils/web/CalendarLinks.hpp"

namespace {
// Pin the LUG time zone for these tests, then put the process back.
struct ChicagoTime {
    std::string old;
    ChicagoTime() { old = process_timezone(); set_process_timezone("America/Chicago"); }
    ~ChicagoTime() {
        if (!old.empty()) set_process_timezone(old);
        else { std::lock_guard<std::mutex> l(tz_env_mutex()); unsetenv("TZ"); tzset(); }
    }
};
}

TEST(CalendarLinks, GoogleAndOutlookLinks) {
    ChicagoTime tz;
    cal_links::Item m{"Build Night & Pizza", "Bring bricks", "Library", "2099-07-04T19:00:00", "2099-07-04T21:00:00", false, ""};
    std::string g = cal_links::google(m);
    EXPECT_EQ(g.rfind("https://calendar.google.com/calendar/render?action=TEMPLATE&text=Build%20Night%20%26%20Pizza", 0), 0u);
    EXPECT_NE(g.find("&dates=20990705T000000Z%2F20990705T020000Z"), std::string::npos);   // CDT -> UTC
    EXPECT_NE(g.find("&location=Library"), std::string::npos);
    std::string o = cal_links::outlook(m, false);
    EXPECT_EQ(o.rfind("https://outlook.live.com/calendar/0/action/compose?", 0), 0u);
    EXPECT_NE(o.find("startdt=2099-07-05T00%3A00%3A00Z"), std::string::npos);
    EXPECT_EQ(o.find("allday"), std::string::npos);
    EXPECT_EQ(cal_links::outlook(m, true).rfind("https://outlook.office.com/", 0), 0u);

    // Winter: CST is UTC-6
    cal_links::Item w{"Winter", "", "", "2099-01-10T19:00:00", "2099-01-10T20:00:00", false, ""};
    EXPECT_NE(cal_links::google(w).find("20990111T010000Z"), std::string::npos);

    // All-day (events): dates, with the end the day after the last day
    cal_links::Item e{"Show", "", "", "2099-08-01T09:00:00", "2099-08-03T17:00:00", true, ""};
    EXPECT_NE(cal_links::google(e).find("&dates=20990801%2F20990804"), std::string::npos);
    std::string oe = cal_links::outlook(e, true);
    EXPECT_NE(oe.find("startdt=2099-08-01&enddt=2099-08-04&allday=true"), std::string::npos);
    // Month and year roll over
    cal_links::Item ny{"NYE", "", "", "2099-12-31T09:00:00", "2099-12-31T17:00:00", true, ""};
    EXPECT_NE(cal_links::google(ny).find("20991231%2F21000101"), std::string::npos);

    // Subscribe block: escaped, with all the buttons
    std::string sub = cal_links::subscribe_html("/calendar.ics", "Ark \"LUG\"", "x");
    EXPECT_NE(sub.find("data-cal-feed=\"/calendar.ics\""), std::string::npos);
    EXPECT_NE(sub.find("data-cal-name=\"Ark &quot;LUG&quot;\""), std::string::npos);
    for (const char* k : {"webcal", "google", "outlook", "m365"})
        EXPECT_NE(sub.find(std::string("data-cal-link=\"") + k + "\""), std::string::npos) << k;
    EXPECT_NE(sub.find("data-cal-qr"), std::string::npos);
    EXPECT_NE(sub.find("data-copy-target=\"#x-url\""), std::string::npos);
}

TEST_F(IntegrationTest, AddToCalendarMenuAndIcsDownload) {
    ChicagoTime tz;
    Meeting m;
    m.title = "Cal Link Meeting"; m.start_time = "2099-07-04T19:00:00"; m.end_time = "2099-07-04T21:00:00";
    m.location = "Hall"; m.scope = "lug_wide"; m.suppress_discord = true; m.suppress_calendar = true;
    auto mt = meeting_svc->create(m);
    LugEvent e;
    e.title = "Cal Link Show"; e.start_time = "2099-08-01T09:00:00"; e.end_time = "2099-08-03T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);

    auto md = GET("/meetings/" + std::to_string(mt.id), member_token);
    expect_contains(md, "Add to calendar");
    expect_contains(md, "calendar.google.com");
    expect_contains(md, "20990705T000000Z%2F20990705T020000Z");
    expect_contains(md, "outlook.office.com");
    expect_contains(md, "&#x2F;meetings&#x2F;" + std::to_string(mt.id) + "&#x2F;calendar.ics");
    auto ed = GET("/events/" + std::to_string(ev.id), member_token);
    expect_contains(ed, "20990801%2F20990804");

    const std::string mics = "/meetings/" + std::to_string(mt.id) + "/calendar.ics";
    EXPECT_NE(GET(mics).code, 200);                                      // signed in only
    auto r = GET(mics, member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "BEGIN:VCALENDAR");
    expect_contains(r, "SUMMARY:Cal Link Meeting");
    expect_contains(r, "UID:" + mt.ical_uid);                             // same UID as the feeds
    auto er = GET("/events/" + std::to_string(ev.id) + "/calendar.ics", member_token);
    expect_contains(er, "DTSTART;VALUE=DATE:20990801");
    EXPECT_EQ(GET("/events/999999/calendar.ics", member_token).code, 404);
}

TEST_F(IntegrationTest, SubscribeBlocksOnPages) {
    expect_contains(GET("/schedule", member_token), "data-cal-feed=\"/calendar.ics\"");
    expect_contains(GET("/schedule?scope=chapter:" + std::to_string(test_chapter_id), member_token),
                    "data-cal-feed=\"/calendar/chapter/" + std::to_string(test_chapter_id) + "/feed.ics\"");
    expect_contains(GET("/dashboard", member_token), "data-cal-feed=\"/calendar.ics\"");
    expect_contains(GET("/account", member_token), "data-cal-feed=\"/calendar.ics\"");
    expect_contains(GET("/chapters/" + std::to_string(test_chapter_id), member_token),
                    "data-cal-feed=\"/calendar/chapter/" + std::to_string(test_chapter_id) + "/feed.ics\"");
    auto priv = POST("/account/calendar-token", "", member_token);
    EXPECT_EQ(priv.code, 200);
    expect_contains(priv, "data-cal-feed=\"/calendar/me/");
}

TEST_F(IntegrationTest, GoogleButtonUsesPublicSharedCalendar) {
    const std::string cid = "cid=cal-id%40group.calendar.google.com";
    settings_repo->set("google_calendar_id", "cal-id@group.calendar.google.com");
    expect_not_contains(GET("/schedule", member_token), cid);         // not public: the feed link
    settings_repo->set("google_calendar_public", "1");
    for (const char* page : {"/schedule", "/dashboard", "/account"}) {
        auto r = GET(page, member_token);
        expect_contains(r, "https://calendar.google.com/calendar/render?" + cid);
        expect_contains(r, "data-cal-fixed");
        expect_contains(r, "updates right away");
    }
    // A chapter's feed isn't the shared calendar, so its Google button stays on the feed
    expect_not_contains(GET("/schedule?scope=chapter:" + std::to_string(test_chapter_id), member_token), cid);
    expect_not_contains(GET("/chapters/" + std::to_string(test_chapter_id), member_token), cid);
    expect_not_contains(POST("/account/calendar-token", "", member_token), cid);

    // Settings > Google Calendar: the checkbox
    expect_contains(GET("/settings/google-calendar", admin_token), "name=\"google_calendar_public\" value=\"1\" checked");
    POST_HTMX("/settings/google-calendar", "google_service_account_json_path=&google_calendar_id=cal-id%40group.calendar.google.com", admin_token);
    EXPECT_EQ(settings_repo->get("google_calendar_public"), "0");
    expect_not_contains(GET("/schedule", member_token), cid);
    POST_HTMX("/settings/google-calendar", "google_service_account_json_path=&google_calendar_id=cal-id%40group.calendar.google.com&google_calendar_public=1", admin_token);
    EXPECT_EQ(settings_repo->get("google_calendar_public"), "1");
}
