#include "test_helper.hpp"
#include "services/MeetingService.hpp"
#include "services/EventService.hpp"
#include "services/AttendanceService.hpp"
#include "services/ChapterService.hpp"
#include "services/MemberService.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "async/ThreadPool.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Service test fixture with all dependencies
// ═══════════════════════════════════════════════════════════════════════════

class ServiceFixture : public DbFixture {
protected:
    Config config;
    std::unique_ptr<ThreadPool> pool;
    std::unique_ptr<DiscordClient> discord;
    std::unique_ptr<CalendarGenerator> calendar;
    std::unique_ptr<MemberRepository> member_repo;
    std::unique_ptr<MeetingRepository> meeting_repo;
    std::unique_ptr<EventRepository> event_repo;
    std::unique_ptr<ChapterRepository> chapter_repo;
    std::unique_ptr<AttendanceRepository> attendance_repo;
    std::unique_ptr<ChapterMemberRepository> chapter_member_repo;
    std::unique_ptr<MemberService> member_svc;
    std::unique_ptr<MeetingService> meeting_svc;
    std::unique_ptr<EventService> event_svc;
    std::unique_ptr<AttendanceService> attendance_svc;
    std::unique_ptr<ChapterService> chapter_svc;

    void SetUp() override {
        DbFixture::SetUp();
        pool = std::make_unique<ThreadPool>(1);
        discord = std::make_unique<DiscordClient>(config, *pool);
        member_repo = std::make_unique<MemberRepository>(*db);
        meeting_repo = std::make_unique<MeetingRepository>(*db);
        event_repo = std::make_unique<EventRepository>(*db);
        chapter_repo = std::make_unique<ChapterRepository>(*db);
        attendance_repo = std::make_unique<AttendanceRepository>(*db);
        chapter_member_repo = std::make_unique<ChapterMemberRepository>(*db);
        calendar = std::make_unique<CalendarGenerator>(*meeting_repo, *event_repo, config, chapter_repo.get());
        member_svc = std::make_unique<MemberService>(*member_repo);
        meeting_svc = std::make_unique<MeetingService>(*meeting_repo, *discord, *calendar, chapter_repo.get());
        event_svc = std::make_unique<EventService>(*event_repo, *discord, *calendar, chapter_repo.get());
        attendance_svc = std::make_unique<AttendanceService>(*attendance_repo, *member_repo);
        chapter_svc = std::make_unique<ChapterService>(*chapter_repo);
    }

    Member create_member(const std::string& discord_id = "svc-member-1") {
        Member m;
        m.discord_user_id  = discord_id;
        m.discord_username = "svcuser";
        m.display_name     = "Svc User";
        m.role             = "member";
        return member_svc->create(m);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Meeting Service
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ServiceFixture, MeetingCreateGeneratesUuid) {
    Meeting m;
    m.title      = "UUID Test";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope      = "lug_wide";
    auto created = meeting_svc->create(m);
    EXPECT_FALSE(created.ical_uid.empty());
    EXPECT_GT(created.id, 0);
}

TEST_F(ServiceFixture, MeetingUpdateMergesFields) {
    Meeting m;
    m.title      = "Original Title";
    m.description = "Original Desc";
    m.location   = "Original Loc";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope      = "lug_wide";
    auto created = meeting_svc->create(m);

    Meeting updates;
    updates.title = "New Title";
    // description and location empty = keep originals
    auto updated = meeting_svc->update(created.id, updates);
    EXPECT_EQ(updated.title, "New Title");
    EXPECT_EQ(updated.description, "Original Desc");
    EXPECT_EQ(updated.location, "Original Loc");
}

TEST_F(ServiceFixture, MeetingCancelDeletesFromDb) {
    Meeting m;
    m.title      = "To Cancel";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope      = "lug_wide";
    auto created = meeting_svc->create(m);
    EXPECT_TRUE(meeting_svc->get(created.id).has_value());

    meeting_svc->cancel(created.id);
    EXPECT_FALSE(meeting_svc->get(created.id).has_value());
}

TEST_F(ServiceFixture, MeetingCancelNonexistentThrows) {
    EXPECT_THROW(meeting_svc->cancel(99999), std::runtime_error);
}

TEST_F(ServiceFixture, MeetingListPaginated) {
    for (int i = 0; i < 5; ++i) {
        Meeting m;
        m.title      = "Mtg " + std::to_string(i);
        m.start_time = "2026-05-01T19:00:00";
        m.end_time   = "2026-05-01T21:00:00";
        m.scope      = "lug_wide";
        meeting_svc->create(m);
    }
    EXPECT_EQ(meeting_svc->count_all(), 5);
    EXPECT_EQ(meeting_svc->count_filtered(""), 5);
    EXPECT_GE(meeting_svc->count_filtered("Mtg 3"), 1);

    auto page = meeting_svc->list_paginated("", 3, 0);
    EXPECT_EQ(page.size(), 3);
}

TEST_F(ServiceFixture, MeetingScopeChangeUpdatesChapterId) {
    Chapter ch;
    ch.name = "Svc Chapter";
    ch.discord_announcement_channel_id = "test-channel-id";
    auto chapter = chapter_svc->create(ch);

    Meeting m;
    m.title      = "Chapter Mtg";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope      = "chapter";
    m.chapter_id = chapter.id;
    auto created = meeting_svc->create(m);

    // Change to lug_wide — chapter_id should clear
    Meeting updates;
    updates.scope = "lug_wide";
    auto updated = meeting_svc->update(created.id, updates);
    EXPECT_EQ(updated.scope, "lug_wide");
    EXPECT_EQ(updated.chapter_id, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Event Service
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ServiceFixture, EventCreateGeneratesUuid) {
    LugEvent e;
    e.title      = "UUID Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time   = "2026-06-02T00:00:00";
    e.status     = "confirmed";
    e.scope      = "lug_wide";
    auto created = event_svc->create(e);
    EXPECT_FALSE(created.ical_uid.empty());
    EXPECT_GT(created.id, 0);
}

TEST_F(ServiceFixture, EventUpdateMergesFields) {
    LugEvent e;
    e.title       = "Original Event";
    e.description = "Original Desc";
    e.location    = "Original Loc";
    e.start_time  = "2026-06-01T00:00:00";
    e.end_time    = "2026-06-02T00:00:00";
    e.status      = "confirmed";
    e.scope       = "lug_wide";
    auto created = event_svc->create(e);

    LugEvent updates;
    updates.title = "New Event Title";
    auto updated = event_svc->update(created.id, updates);
    EXPECT_EQ(updated.title, "New Event Title");
    EXPECT_EQ(updated.description, "Original Desc");
}

TEST_F(ServiceFixture, EventCancelDeletesFromDb) {
    LugEvent e;
    e.title      = "To Delete";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time   = "2026-06-02T00:00:00";
    e.status     = "confirmed";
    e.scope      = "lug_wide";
    auto created = event_svc->create(e);
    event_svc->cancel(created.id);
    EXPECT_FALSE(event_svc->get(created.id).has_value());
}

TEST_F(ServiceFixture, EventStatusUpdate) {
    LugEvent e;
    e.title      = "Status Test";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time   = "2026-06-02T00:00:00";
    e.status     = "confirmed";
    e.scope      = "lug_wide";
    auto created = event_svc->create(e);

    event_svc->update_status(created.id, "tentative");
    auto found = event_svc->get(created.id);
    EXPECT_EQ(found->status, "tentative");

    event_svc->update_status(created.id, "open");
    found = event_svc->get(created.id);
    EXPECT_EQ(found->status, "open");
}

TEST_F(ServiceFixture, EventCreateImported) {
    LugEvent e;
    e.title      = "Imported Event";
    e.start_time = "2026-07-01T00:00:00";
    e.end_time   = "2026-07-02T00:00:00";
    e.status     = "confirmed";
    e.scope      = "lug_wide";
    e.google_calendar_event_id = "gcal-imported-1";
    auto created = event_svc->create_imported(e);
    EXPECT_GT(created.id, 0);
    EXPECT_EQ(created.google_calendar_event_id, "gcal-imported-1");
    EXPECT_TRUE(event_svc->exists_by_google_calendar_id("gcal-imported-1"));
}

TEST_F(ServiceFixture, EventCalendarTitle) {
    Chapter ch;
    ch.name = "NWA Chapter";
    ch.shorthand = "NWA";
    ch.discord_announcement_channel_id = "test-ch-id";
    auto chapter = chapter_svc->create(ch);

    LugEvent e;
    e.title      = "Showcase";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time   = "2026-06-02T00:00:00";
    e.status     = "tentative";
    e.scope      = "chapter";
    e.chapter_id = chapter.id;
    auto created = event_svc->create(e);

    auto cal = event_svc->with_calendar_title(created);
    EXPECT_NE(cal.title.find("[NWA]"), std::string::npos);
    EXPECT_NE(cal.title.find("[Tentative]"), std::string::npos);
    EXPECT_NE(cal.title.find("Showcase"), std::string::npos);
}

TEST_F(ServiceFixture, EventCalendarTitleNonLug) {
    LugEvent e;
    e.title  = "BrickFair";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time   = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope  = "non_lug";
    auto created = event_svc->create(e);
    auto cal = event_svc->with_calendar_title(created);
    EXPECT_NE(cal.title.find("[Non-LUG]"), std::string::npos);
}

TEST_F(ServiceFixture, EventCalendarTitleLugWide) {
    LugEvent e;
    e.title  = "Annual Show";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time   = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope  = "lug_wide";
    auto created = event_svc->create(e);
    auto cal = event_svc->with_calendar_title(created);
    EXPECT_NE(cal.title.find("[LUG Wide]"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Attendance Service
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ServiceFixture, AttendanceCheckInOut) {
    auto member = create_member();
    Meeting m;
    m.title = "Att Svc Mtg";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    EXPECT_TRUE(attendance_svc->check_in(member.id, "meeting", mtg.id));
    EXPECT_TRUE(attendance_svc->is_checked_in(member.id, "meeting", mtg.id));
    EXPECT_EQ(attendance_svc->get_count("meeting", mtg.id), 1);

    attendance_svc->check_out(member.id, "meeting", mtg.id);
    EXPECT_FALSE(attendance_svc->is_checked_in(member.id, "meeting", mtg.id));
}

TEST_F(ServiceFixture, AttendanceVirtualToggle) {
    auto member = create_member();
    Meeting m;
    m.title = "Virtual Mtg";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    attendance_svc->check_in(member.id, "meeting", mtg.id, "", true);
    auto attendees = attendance_svc->get_attendees("meeting", mtg.id);
    ASSERT_EQ(attendees.size(), 1);
    EXPECT_TRUE(attendees[0].is_virtual);

    attendance_svc->set_virtual(attendees[0].id, false);
    attendees = attendance_svc->get_attendees("meeting", mtg.id);
    EXPECT_FALSE(attendees[0].is_virtual);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chapter Service
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ServiceFixture, ChapterCrud) {
    Chapter ch;
    ch.name = "Svc Test Chapter";
    ch.shorthand = "STC";
    ch.description = "A service test chapter";
    ch.discord_announcement_channel_id = "test-ch-id";
    auto created = chapter_svc->create(ch);
    EXPECT_GT(created.id, 0);
    EXPECT_EQ(created.shorthand, "STC");

    Chapter updates = created;
    updates.name = "Updated Chapter";
    updates.shorthand = "UC";
    auto updated = chapter_svc->update(created.id, updates);
    EXPECT_EQ(updated.name, "Updated Chapter");
    EXPECT_EQ(updated.shorthand, "UC");

    chapter_svc->delete_chapter(created.id);
    EXPECT_FALSE(chapter_svc->get(created.id).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Calendar Generator
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(ServiceFixture, CalendarGeneratesValidIcs) {
    Meeting m;
    m.title      = "Cal Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope      = "lug_wide";
    meeting_svc->create(m);

    LugEvent e;
    e.title      = "Cal Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time   = "2026-06-02T00:00:00";
    e.status     = "confirmed";
    e.scope      = "lug_wide";
    event_svc->create(e);

    std::string ics = calendar->get_ics();
    EXPECT_NE(ics.find("BEGIN:VCALENDAR"), std::string::npos);
    EXPECT_NE(ics.find("END:VCALENDAR"), std::string::npos);
    EXPECT_NE(ics.find("Cal Meeting"), std::string::npos);
    EXPECT_NE(ics.find("Cal Event"), std::string::npos);
    EXPECT_NE(ics.find("DTSTAMP"), std::string::npos);
}

TEST_F(ServiceFixture, CalendarEventsAreAllDay) {
    LugEvent e;
    e.title      = "All Day Test";
    e.start_time = "2026-06-15T00:00:00";
    e.end_time   = "2026-06-17T00:00:00";
    e.status     = "confirmed";
    e.scope      = "lug_wide";
    event_svc->create(e);

    std::string ics = calendar->get_ics();
    EXPECT_NE(ics.find("DTSTART;VALUE=DATE:20260615"), std::string::npos);
    EXPECT_NE(ics.find("DTEND;VALUE=DATE:20260618"), std::string::npos); // exclusive end
}

TEST_F(ServiceFixture, CalendarMeetingsHaveTime) {
    Meeting m;
    m.title      = "Timed Meeting";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope      = "lug_wide";
    meeting_svc->create(m);

    std::string ics = calendar->get_ics();
    EXPECT_NE(ics.find("20260501T190000"), std::string::npos);
    // Should NOT have VALUE=DATE
    EXPECT_EQ(ics.find("VALUE=DATE:20260501"), std::string::npos);
}

TEST_F(ServiceFixture, CalendarTentativeStatus) {
    LugEvent e;
    e.title      = "Tentative Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time   = "2026-06-02T00:00:00";
    e.status     = "tentative";
    e.scope      = "lug_wide";
    event_svc->create(e);

    std::string ics = calendar->get_ics();
    EXPECT_NE(ics.find("STATUS:TENTATIVE"), std::string::npos);
    EXPECT_NE(ics.find("[Tentative]"), std::string::npos);
}

TEST_F(ServiceFixture, CalendarInvalidateRefreshes) {
    Meeting m;
    m.title      = "First";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time   = "2026-05-01T21:00:00";
    m.scope      = "lug_wide";
    meeting_svc->create(m);

    std::string ics1 = calendar->get_ics();
    EXPECT_NE(ics1.find("First"), std::string::npos);

    // Cached — add another meeting but don't invalidate
    Meeting m2;
    m2.title      = "Second";
    m2.start_time = "2026-06-01T19:00:00";
    m2.end_time   = "2026-06-01T21:00:00";
    m2.scope      = "lug_wide";
    m2.ical_uid   = "second-uid";
    meeting_repo->create(m2); // bypass service to skip invalidation

    std::string ics_cached = calendar->get_ics();
    // Should still be cached (no "Second" yet)
    EXPECT_EQ(ics_cached.find("Second"), std::string::npos);

    calendar->invalidate();
    std::string ics_fresh = calendar->get_ics();
    EXPECT_NE(ics_fresh.find("Second"), std::string::npos);
}
