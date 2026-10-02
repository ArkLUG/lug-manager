// Schedule: meetings and events together, list and calendar views, filters.
#include "integration_test_base.hpp"

namespace {
Meeting meeting(const std::string& title, const std::string& start, const std::string& scope, int64_t chapter_id = 0) {
    Meeting m;
    m.title = title;
    m.start_time = start;
    m.end_time = start.substr(0, 11) + "21:00:00";
    m.scope = scope;
    if (chapter_id) m.chapter_id = chapter_id;
    m.suppress_discord = true;
    m.suppress_calendar = true;
    return m;
}
LugEvent event(const std::string& title, const std::string& start, const std::string& end,
               const std::string& scope = "lug_wide", const std::string& status = "confirmed") {
    LugEvent e;
    e.title = title;
    e.start_time = start;
    e.end_time = end;
    e.scope = scope;
    e.status = status;
    e.suppress_discord = true;
    e.suppress_calendar = true;
    return e;
}
}

TEST_F(IntegrationTest, ScheduleListShowsMeetingsAndEvents) {
    meeting_svc->create(meeting("Sched Group Mtg", "2099-03-04T19:00:00", "lug_wide"));
    event_svc->create(event("Sched Big Show", "2099-03-10T10:00:00", "2099-03-12T16:00:00"));
    meeting_svc->create(meeting("Sched Old Mtg", "2020-01-01T19:00:00", "lug_wide"));

    EXPECT_NE(GET("/schedule").code, 200);                       // signed in only
    auto r = GET("/schedule", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Sched Group Mtg");
    expect_contains(r, "Sched Big Show");
    expect_contains(r, "March 2099");
    expect_contains(r, "Group-wide");
    expect_contains(r, "&#x2F;meetings&#x2F;");  // links are HTML-escaped
    expect_not_contains(r, "Sched Old Mtg");                     // upcoming by default
    // The meeting comes before the event
    EXPECT_LT(r.body.find("Sched Group Mtg"), r.body.find("Sched Big Show"));

    auto past = GET("/schedule?when=past", member_token);
    expect_contains(past, "Sched Old Mtg");
    expect_not_contains(past, "Sched Big Show");

    auto only_events = GET("/schedule?type=events", member_token);
    expect_contains(only_events, "Sched Big Show");
    expect_not_contains(only_events, "Sched Group Mtg");

    auto search = GET("/schedule?q=Big", member_token);
    expect_contains(search, "Sched Big Show");
    expect_not_contains(search, "Sched Group Mtg");
}

TEST_F(IntegrationTest, ScheduleScopeAndStatusFilters) {
    meeting_svc->create(meeting("Sched Chapter Mtg", "2099-04-01T19:00:00", "chapter", test_chapter_id));
    event_svc->create(event("Sched External Con", "2099-04-05T10:00:00", "2099-04-05T16:00:00", "non_lug"));
    event_svc->create(event("Sched Maybe Show", "2099-04-07T10:00:00", "2099-04-07T16:00:00", "lug_wide", "tentative"));
    event_svc->create(event("Sched Off Show", "2099-04-09T10:00:00", "2099-04-09T16:00:00", "lug_wide", "cancelled"));

    auto ext = GET("/schedule?scope=external", member_token);
    expect_contains(ext, "Sched External Con");
    expect_contains(ext, "External");
    expect_not_contains(ext, "Sched Chapter Mtg");

    auto ch = GET("/schedule?scope=chapter:" + std::to_string(test_chapter_id), member_token);
    expect_contains(ch, "Sched Chapter Mtg");
    expect_contains(ch, "Permission Test Chapter");
    expect_not_contains(ch, "Sched External Con");

    auto def = GET("/schedule", member_token);
    expect_contains(def, "Sched Maybe Show");
    expect_contains(def, "Tentative");
    expect_not_contains(def, "Sched Off Show");                  // cancelled hidden by default
    expect_contains(GET("/schedule?status=cancelled", member_token), "Sched Off Show");
    auto tent = GET("/schedule?status=tentative", member_token);
    expect_contains(tent, "Sched Maybe Show");
    expect_not_contains(tent, "Sched External Con");

    // Junk filter values fall back to the defaults instead of reaching SQL
    EXPECT_EQ(GET("/schedule?scope=chapter:1%20OR%201=1&type=x&status=y&when=z", member_token).code, 200);

    // Without chapters, the chapter filter is not offered
    Features::set("chapters", false);
    auto off = GET("/schedule", member_token);
    expect_not_contains(off, "Any chapter");
    Features::set("chapters", true);
    expect_contains(GET("/schedule", member_token), "Any chapter");
}

TEST_F(IntegrationTest, ScheduleMineFilter) {
    auto mine = event_svc->create(event("Sched Going Show", "2099-05-01T10:00:00", "2099-05-01T16:00:00"));
    event_svc->create(event("Sched Other Show", "2099-05-02T10:00:00", "2099-05-02T16:00:00"));
    meeting_svc->create(meeting("Sched Lead Chapter Mtg", "2099-05-03T19:00:00", "chapter", test_chapter_id));
    EXPECT_EQ(POST("/events/" + std::to_string(mine.id) + "/rsvp", "", member_token).code, 200);

    auto r = GET("/schedule?mine=1", member_token);
    expect_contains(r, "Sched Going Show");
    expect_not_contains(r, "Sched Other Show");
    expect_not_contains(r, "Sched Lead Chapter Mtg");            // not in that chapter
    expect_contains(GET("/schedule?mine=1", chapter_lead_token), "Sched Lead Chapter Mtg");
}

TEST_F(IntegrationTest, ScheduleCalendarMonth) {
    meeting_svc->create(meeting("Sched Cal Mtg", "2099-06-15T19:00:00", "lug_wide"));
    event_svc->create(event("Sched Cal Weekend", "2099-06-19T10:00:00", "2099-06-21T16:00:00"));

    auto r = GET("/schedule?view=calendar&month=2099-06", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "June 2099");
    expect_contains(r, "Sched Cal Mtg");
    expect_contains(r, "7:00 PM");
    // A three-day event is on each of its days
    size_t n = 0;
    for (size_t p = r.body.find("Sched Cal Weekend"); p != std::string::npos; p = r.body.find("Sched Cal Weekend", p + 1)) ++n;
    EXPECT_GE(n, 3u);
    // Month navigation keeps the filters
    expect_contains(r, "month&#x3D;2099-07");
    expect_contains(r, "month&#x3D;2099-05");
    auto filtered = GET("/schedule?view=calendar&month=2099-06&type=meetings", member_token);
    expect_not_contains(filtered, "Sched Cal Weekend");
    expect_contains(filtered, "type&#x3D;meetings&amp;");

    // December rolls into the next year; nonsense months fall back to now
    expect_contains(GET("/schedule?view=calendar&month=2099-12", member_token), "month&#x3D;2100-01");
    EXPECT_EQ(GET("/schedule?view=calendar&month=2099-13", member_token).code, 200);
}

TEST_F(IntegrationTest, ScheduleNewMenuAndSidebar) {
    auto lead = GET("/schedule", event_manager_token);
    expect_contains(lead, "/meetings/new");
    expect_contains(lead, "/events/new");
    expect_not_contains(GET("/schedule", member_token), "/meetings/new");

    // Sidebar: one Schedule item, highlighted on the meetings and events pages too
    auto dash = GET("/dashboard", member_token);
    expect_contains(dash, "hx-get=\"/schedule\"");
    expect_not_contains(dash, "hx-get=\"/meetings\"\n");
    auto meetings = GET("/meetings", member_token);
    expect_contains(meetings, "&larr; Schedule");
}
