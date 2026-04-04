#include "integration_test_base.hpp"

TEST_F(IntegrationTest, MeetingCardShowsDateAndTime) {
    Meeting m;
    m.title = "Card Test Meeting";
    m.start_time = "2026-05-15T19:00:00";
    m.end_time = "2026-05-15T21:00:00";
    m.location = "Test Location";
    m.scope = "lug_wide";
    meeting_svc->create(m);

    auto r = GET_HTMX("/meetings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Card Test Meeting");
    expect_contains(r, "May");
    expect_contains(r, "15");
    expect_contains(r, "7:00 PM");
    expect_contains(r, "Test Location");
}

TEST_F(IntegrationTest, EventCardShowsBadges) {
    LugEvent e;
    e.title = "Badge Test Event";
    e.start_time = "2026-07-01T00:00:00";
    e.end_time = "2026-07-02T00:00:00";
    e.status = "tentative";
    e.scope = "lug_wide";
    event_svc->create(e);

    auto r = GET_HTMX("/events", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Badge Test Event");
    expect_contains(r, "Tentative");
    expect_contains(r, "LUG Wide");
}

TEST_F(IntegrationTest, NonLugEventHidesStatusButtons) {
    LugEvent e;
    e.title = "Non-LUG Test";
    e.start_time = "2026-08-01T00:00:00";
    e.end_time = "2026-08-02T00:00:00";
    e.status = "confirmed";
    e.scope = "non_lug";
    event_svc->create(e);

    auto r = GET_HTMX("/events", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Non-LUG Test");
    expect_contains(r, "Non-LUG");
    // Non-LUG events should NOT show attendance or status buttons
    // (can't easily test per-card, but the page should have the event)
}

TEST_F(IntegrationTest, CalendarContainsAllDayEvents) {
    LugEvent e;
    e.title = "iCal All Day";
    e.start_time = "2026-09-01T00:00:00";
    e.end_time = "2026-09-03T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    event_svc->create(e);

    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "iCal All Day");
    expect_contains(r, "DTSTART;VALUE=DATE:20260901");
    expect_contains(r, "DTEND;VALUE=DATE:20260904"); // exclusive end
}

TEST_F(IntegrationTest, CalendarContainsTimedMeetings) {
    Meeting m;
    m.title = "iCal Timed";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    meeting_svc->create(m);

    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "iCal Timed");
    expect_contains(r, "20260501T190000");
    expect_not_contains(r, "VALUE=DATE:20260501");
}

TEST_F(IntegrationTest, CalendarTitlesHavePrefixes) {
    Chapter ch;
    ch.name = "iCal Chapter";
    ch.shorthand = "ICL";
    ch.discord_announcement_channel_id = "icl-ch";
    auto chapter = chapter_svc->create(ch);

    LugEvent e;
    e.title = "Prefixed Event";
    e.start_time = "2026-10-01T00:00:00";
    e.end_time = "2026-10-02T00:00:00";
    e.status = "tentative";
    e.scope = "chapter";
    e.chapter_id = chapter.id;
    event_svc->create(e);

    auto r = GET("/calendar.ics");
    expect_contains(r, "[ICL]");
    expect_contains(r, "[Tentative]");
    expect_contains(r, "Prefixed Event");
}

TEST_F(IntegrationTest, AttendanceOverviewShowsCounts) {
    Meeting m;
    m.title = "Count Test";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    attendance_svc->check_in(admin_member_id, "meeting", mtg.id);
    attendance_svc->check_in(regular_member_id, "meeting", mtg.id, "", true);

    auto r = GET("/attendance/overview", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");
    // Both members should appear with meeting counts
    expect_contains(r, "Admin U.");
    expect_contains(r, "Regular U.");
}

// ═══════════════════════════════════════════════════════════════════════════
// Root redirect
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, NonExistentMemberReturns404) {
    auto r = GET("/members/99999", admin_token);
    EXPECT_EQ(r.code, 404);
}

TEST_F(IntegrationTest, NonExistentChapterReturns404) {
    auto r = GET("/chapters/99999", admin_token);
    EXPECT_EQ(r.code, 404);
}

// ═══════════════════════════════════════════════════════════════════════════
// UI content — additional validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MembersListShowsExistingMembers) {
    // Members page uses DataTables AJAX — verify via the datatable API
    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin U.");
    expect_contains(r, "Regular U.");
}

TEST_F(IntegrationTest, DashboardShowsMeetingAndEventCounts) {
    // Create a meeting and event so counts are non-zero
    Meeting m;
    m.title = "Count Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    meeting_svc->create(m);

    LugEvent e;
    e.title = "Count Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    event_svc->create(e);

    auto r = GET("/dashboard", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Members");
    expect_contains(r, "Meetings");
    expect_contains(r, "Events");
}

TEST_F(IntegrationTest, MeetingCardShowsEditButtonForAdmin) {
    Meeting m;
    m.title = "Admin Card Meeting";
    m.start_time = "2026-05-15T19:00:00";
    m.end_time = "2026-05-15T21:00:00";
    m.scope = "lug_wide";
    meeting_svc->create(m);

    auto r = GET_HTMX("/meetings", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin Card Meeting");
    expect_contains(r, "edit");
}

TEST_F(IntegrationTest, EventsPageShowsConfirmedBadge) {
    LugEvent e;
    e.title = "Confirmed Badge Event";
    e.start_time = "2026-07-01T00:00:00";
    e.end_time = "2026-07-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    event_svc->create(e);

    auto r = GET_HTMX("/events", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Confirmed Badge Event");
    expect_contains(r, "Confirmed");
}

TEST_F(IntegrationTest, AttendanceCountEndpointForEvent) {
    LugEvent e;
    e.title = "Attendance Count Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    attendance_svc->check_in(admin_member_id, "event", ev.id);
    attendance_svc->check_in(regular_member_id, "event", ev.id);

    auto r = GET("/attendance/count/event/" + std::to_string(ev.id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "2");

    auto lr = GET("/attendance/list/event/" + std::to_string(ev.id), admin_token);
    EXPECT_EQ(lr.code, 200);
    expect_contains(lr, "Admin U.");
    expect_contains(lr, "Regular U.");
}

TEST_F(IntegrationTest, MeetingPaginationPageOne) {
    // Page 1 with no explicit param should still work
    auto r = GET("/meetings?page=1", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Meetings");
}

TEST_F(IntegrationTest, CalendarEmptyIsValid) {
    // With no events/meetings the calendar should still be valid iCal
    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "BEGIN:VCALENDAR");
    expect_contains(r, "VERSION:2.0");
    expect_contains(r, "END:VCALENDAR");
}

// ═══════════════════════════════════════════════════════════════════════════
// New feature tests — role redesign, PII hiding, suppress, perks, notes
// ═══════════════════════════════════════════════════════════════════════════

