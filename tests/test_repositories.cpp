#include "test_helper.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/ChapterRepository.hpp"
#include "repositories/MeetingRepository.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Member Repository
// ═══════════════════════════════════════════════════════════════════════════

class MemberRepoTest : public DbFixture {
protected:
    std::unique_ptr<MemberRepository> repo;
    void SetUp() override {
        DbFixture::SetUp();
        repo = std::make_unique<MemberRepository>(*db);
    }

    Member make_member(const std::string& discord_id = "123456",
                       const std::string& name = "Test User") {
        Member m;
        m.discord_user_id  = discord_id;
        m.discord_username = "testuser";
        m.display_name     = name;
        m.email            = "test@example.com";
        m.role             = "member";
        return m;
    }
};

TEST_F(MemberRepoTest, CreateAndFindById) {
    auto created = repo->create(make_member());
    ASSERT_GT(created.id, 0);
    EXPECT_EQ(created.display_name, "Test User");

    auto found = repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->discord_user_id, "123456");
    EXPECT_EQ(found->display_name, "Test User");
}

TEST_F(MemberRepoTest, FindByDiscordId) {
    repo->create(make_member("999"));
    auto found = repo->find_by_discord_id("999");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->discord_user_id, "999");

    auto missing = repo->find_by_discord_id("000");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(MemberRepoTest, UpdateMember) {
    auto created = repo->create(make_member());
    created.display_name = "Updated Name";
    created.email = "new@example.com";
    created.role = "admin";
    repo->update(created);
    auto found = repo->find_by_id(created.id);
    EXPECT_EQ(found->display_name, "Updated Name");
    EXPECT_EQ(found->email, "new@example.com");
    EXPECT_EQ(found->role, "admin");
}

TEST_F(MemberRepoTest, DeleteMember) {
    auto created = repo->create(make_member());
    EXPECT_EQ(count("members"), 1);
    repo->delete_by_id(created.id);
    EXPECT_EQ(count("members"), 0);
}

TEST_F(MemberRepoTest, DuplicateDiscordIdFails) {
    repo->create(make_member("111"));
    EXPECT_THROW(repo->create(make_member("111")), DbError);
}

TEST_F(MemberRepoTest, SetPaidStatus) {
    auto created = repo->create(make_member());
    repo->set_paid(created.id, true, "2027-03-31");
    auto found = repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->is_paid);
    EXPECT_EQ(found->paid_until, "2027-03-31");
}

TEST_F(MemberRepoTest, FindAllAndCount) {
    repo->create(make_member("a"));
    repo->create(make_member("b"));
    repo->create(make_member("c"));
    auto all = repo->find_all();
    EXPECT_EQ(all.size(), 3);
    EXPECT_EQ(repo->count_all(), 3);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chapter Repository
// ═══════════════════════════════════════════════════════════════════════════

class ChapterRepoTest : public DbFixture {
protected:
    std::unique_ptr<ChapterRepository> repo;
    void SetUp() override {
        DbFixture::SetUp();
        repo = std::make_unique<ChapterRepository>(*db);
    }
};

TEST_F(ChapterRepoTest, CreateAndFind) {
    Chapter ch;
    ch.name = "Test Chapter";
    ch.shorthand = "TC";
    ch.description = "A test chapter";
    auto created = repo->create(ch);
    ASSERT_GT(created.id, 0);
    EXPECT_EQ(created.name, "Test Chapter");
    EXPECT_EQ(created.shorthand, "TC");

    auto found = repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->shorthand, "TC");
}

TEST_F(ChapterRepoTest, UpdateChapter) {
    Chapter ch;
    ch.name = "Original";
    ch.shorthand = "OR";
    auto created = repo->create(ch);

    Chapter updates = created;
    updates.name = "Updated";
    updates.shorthand = "UP";
    repo->update(updates);

    auto found = repo->find_by_id(created.id);
    EXPECT_EQ(found->name, "Updated");
    EXPECT_EQ(found->shorthand, "UP");
}

TEST_F(ChapterRepoTest, DeleteChapter) {
    Chapter ch;
    ch.name = "Deletable";
    auto created = repo->create(ch);
    EXPECT_EQ(count("chapters"), 1);
    repo->delete_by_id(created.id);
    EXPECT_EQ(count("chapters"), 0);
}

TEST_F(ChapterRepoTest, FindAll) {
    Chapter a; a.name = "Alpha";
    Chapter b; b.name = "Beta";
    repo->create(a);
    repo->create(b);
    auto all = repo->find_all();
    EXPECT_EQ(all.size(), 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// Meeting Repository
// ═══════════════════════════════════════════════════════════════════════════

class MeetingRepoTest : public DbFixture {
protected:
    std::unique_ptr<MeetingRepository> repo;
    void SetUp() override {
        DbFixture::SetUp();
        repo = std::make_unique<MeetingRepository>(*db);
    }

    Meeting make_meeting(const std::string& title = "Test Meeting") {
        Meeting m;
        m.title       = title;
        m.description = "A meeting";
        m.location    = "Room 101";
        m.start_time  = "2026-04-15T19:00:00";
        m.end_time    = "2026-04-15T21:00:00";
        m.status      = "scheduled";
        m.ical_uid    = "test-uid-" + title;
        m.scope       = "lug_wide";
        return m;
    }
};

TEST_F(MeetingRepoTest, CreateAndFind) {
    auto created = repo->create(make_meeting());
    ASSERT_GT(created.id, 0);
    EXPECT_EQ(created.title, "Test Meeting");

    auto found = repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->location, "Room 101");
    EXPECT_EQ(found->scope, "lug_wide");
}

TEST_F(MeetingRepoTest, UpdateMeeting) {
    auto created = repo->create(make_meeting());
    Meeting updated = created;
    updated.title = "Updated Meeting";
    updated.location = "Room 202";
    repo->update(updated);

    auto found = repo->find_by_id(created.id);
    EXPECT_EQ(found->title, "Updated Meeting");
    EXPECT_EQ(found->location, "Room 202");
}

TEST_F(MeetingRepoTest, DeleteCascadesAttendance) {
    auto mtg = repo->create(make_meeting());
    // Create a member for attendance
    MemberRepository members(*db);
    Member m;
    m.discord_user_id = "att1";
    m.discord_username = "att";
    m.display_name = "Attendee";
    m.role = "member";
    auto member = members.create(m);

    AttendanceRepository att(*db);
    att.check_in(member.id, "meeting", mtg.id);
    EXPECT_EQ(count("attendance"), 1);

    repo->delete_by_id(mtg.id);
    EXPECT_EQ(count("attendance"), 0);
    EXPECT_EQ(count("meetings"), 0);
}

TEST_F(MeetingRepoTest, FindUpcoming) {
    auto past = make_meeting("Past");
    past.start_time = "2020-01-01T10:00:00";
    repo->create(past);

    auto future = make_meeting("Future");
    future.start_time = "2030-06-15T19:00:00";
    repo->create(future);

    auto upcoming = repo->find_upcoming();
    EXPECT_EQ(upcoming.size(), 1);
    EXPECT_EQ(upcoming[0].title, "Future");
}

TEST_F(MeetingRepoTest, PaginatedSearch) {
    for (int i = 0; i < 15; ++i)
        repo->create(make_meeting("Meeting " + std::to_string(i)));

    auto page1 = repo->find_paginated("", 10, 0);
    EXPECT_EQ(page1.size(), 10);

    auto page2 = repo->find_paginated("", 10, 10);
    EXPECT_EQ(page2.size(), 5);

    auto searched = repo->find_paginated("Meeting 3", 10, 0);
    EXPECT_GE(searched.size(), 1);

    EXPECT_EQ(repo->count_filtered(""), 15);
    EXPECT_GE(repo->count_filtered("Meeting 3"), 1);
}

TEST_F(MeetingRepoTest, ChapterIdNullWhenZero) {
    auto m = make_meeting();
    m.chapter_id = 0;
    auto created = repo->create(m);
    auto found = repo->find_by_id(created.id);
    EXPECT_EQ(found->chapter_id, 0);
}

TEST_F(MeetingRepoTest, GoogleCalendarEventId) {
    auto created = repo->create(make_meeting());
    EXPECT_TRUE(created.google_calendar_event_id.empty());

    repo->update_google_calendar_event_id(created.id, "gcal123");
    auto found = repo->find_by_id(created.id);
    EXPECT_EQ(found->google_calendar_event_id, "gcal123");
    EXPECT_TRUE(repo->exists_by_google_calendar_id("gcal123"));
    EXPECT_FALSE(repo->exists_by_google_calendar_id("nonexistent"));
}

// ═══════════════════════════════════════════════════════════════════════════
// Event Repository
// ═══════════════════════════════════════════════════════════════════════════

class EventRepoTest : public DbFixture {
protected:
    std::unique_ptr<EventRepository> repo;
    void SetUp() override {
        DbFixture::SetUp();
        repo = std::make_unique<EventRepository>(*db);
    }

    LugEvent make_event(const std::string& title = "Test Event") {
        LugEvent e;
        e.title       = title;
        e.description = "An event";
        e.location    = "Convention Center";
        e.start_time  = "2026-06-01T00:00:00";
        e.end_time    = "2026-06-03T00:00:00";
        e.status      = "confirmed";
        e.ical_uid    = "evt-uid-" + title;
        e.scope       = "lug_wide";
        return e;
    }
};

TEST_F(EventRepoTest, CreateAndFind) {
    auto created = repo->create(make_event());
    ASSERT_GT(created.id, 0);
    EXPECT_EQ(created.title, "Test Event");
    EXPECT_EQ(created.status, "confirmed");

    auto found = repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->location, "Convention Center");
}

TEST_F(EventRepoTest, StatusCheckConstraint) {
    auto e = make_event();
    e.status = "tentative";
    auto created = repo->create(e);
    EXPECT_EQ(created.status, "tentative");

    // Invalid status should fail
    auto bad = make_event("Bad");
    bad.status = "invalid_status";
    EXPECT_THROW(repo->create(bad), DbError);
}

TEST_F(EventRepoTest, UpdateWithChapterId) {
    ChapterRepository chapters(*db);
    Chapter ch;
    ch.name = "TestChap";
    auto chapter = chapters.create(ch);

    auto created = repo->create(make_event());
    LugEvent updated = created;
    updated.chapter_id = chapter.id;
    updated.scope = "chapter";
    repo->update(updated);

    auto found = repo->find_by_id(created.id);
    EXPECT_EQ(found->chapter_id, chapter.id);
    EXPECT_EQ(found->scope, "chapter");
}

TEST_F(EventRepoTest, ChapterIdNullWhenZero) {
    auto created = repo->create(make_event());
    LugEvent updated = created;
    updated.chapter_id = 0;
    repo->update(updated);

    auto found = repo->find_by_id(created.id);
    EXPECT_EQ(found->chapter_id, 0);
}

TEST_F(EventRepoTest, DeleteCascadesAttendance) {
    auto ev = repo->create(make_event());
    MemberRepository members(*db);
    Member m;
    m.discord_user_id = "ev-att1";
    m.discord_username = "evatt";
    m.display_name = "Event Attendee";
    m.role = "member";
    auto member = members.create(m);

    AttendanceRepository att(*db);
    att.check_in(member.id, "event", ev.id);
    EXPECT_EQ(count("attendance"), 1);

    repo->delete_by_id(ev.id);
    EXPECT_EQ(count("attendance"), 0);
}

TEST_F(EventRepoTest, GoogleCalendarEventId) {
    auto created = repo->create(make_event());
    repo->update_google_calendar_event_id(created.id, "gcal-ev-1");
    auto found = repo->find_by_id(created.id);
    EXPECT_EQ(found->google_calendar_event_id, "gcal-ev-1");
    EXPECT_TRUE(repo->exists_by_google_calendar_id("gcal-ev-1"));
}

TEST_F(EventRepoTest, PaginatedSearch) {
    for (int i = 0; i < 12; ++i)
        repo->create(make_event("Event " + std::to_string(i)));

    auto page = repo->find_paginated("", 5, 0, false);
    EXPECT_EQ(page.size(), 5);
    EXPECT_EQ(repo->count_filtered("", false), 12);
    EXPECT_GE(repo->count_filtered("Event 5", false), 1);
}

TEST_F(EventRepoTest, FindUpcomingOnly) {
    auto past = make_event("Past");
    past.start_time = "2020-01-01T00:00:00";
    repo->create(past);

    auto future = make_event("Future");
    future.start_time = "2030-06-15T00:00:00";
    repo->create(future);

    auto upcoming = repo->find_upcoming();
    EXPECT_EQ(upcoming.size(), 1);
    EXPECT_EQ(upcoming[0].title, "Future");
}

// ═══════════════════════════════════════════════════════════════════════════
// Attendance Repository
// ═══════════════════════════════════════════════════════════════════════════

class AttendanceRepoTest : public DbFixture {
protected:
    std::unique_ptr<AttendanceRepository> att;
    std::unique_ptr<MemberRepository> members;
    std::unique_ptr<MeetingRepository> meetings;
    int64_t member_id = 0;
    int64_t meeting_id = 0;

    void SetUp() override {
        DbFixture::SetUp();
        att = std::make_unique<AttendanceRepository>(*db);
        members = std::make_unique<MemberRepository>(*db);
        meetings = std::make_unique<MeetingRepository>(*db);

        Member m;
        m.discord_user_id = "att-test";
        m.discord_username = "atttest";
        m.display_name = "Att Tester";
        m.role = "member";
        member_id = members->create(m).id;

        Meeting mtg;
        mtg.title = "Att Meeting";
        mtg.start_time = "2026-05-01T19:00:00";
        mtg.end_time   = "2026-05-01T21:00:00";
        mtg.ical_uid   = "att-mtg-uid";
        mtg.scope      = "lug_wide";
        meeting_id = meetings->create(mtg).id;
    }
};

TEST_F(AttendanceRepoTest, CheckInAndOut) {
    bool ok = att->check_in(member_id, "meeting", meeting_id);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(att->is_checked_in(member_id, "meeting", meeting_id));
    EXPECT_EQ(att->count_by_entity("meeting", meeting_id), 1);

    att->check_out(member_id, "meeting", meeting_id);
    EXPECT_FALSE(att->is_checked_in(member_id, "meeting", meeting_id));
    EXPECT_EQ(att->count_by_entity("meeting", meeting_id), 0);
}

TEST_F(AttendanceRepoTest, DuplicateCheckInIgnored) {
    att->check_in(member_id, "meeting", meeting_id);
    att->check_in(member_id, "meeting", meeting_id); // should not throw or duplicate
    EXPECT_EQ(att->count_by_entity("meeting", meeting_id), 1);
}

TEST_F(AttendanceRepoTest, VirtualAttendance) {
    att->check_in(member_id, "meeting", meeting_id, "notes", true);
    auto attendees = att->find_by_entity("meeting", meeting_id);
    ASSERT_EQ(attendees.size(), 1);
    EXPECT_TRUE(attendees[0].is_virtual);
    EXPECT_EQ(attendees[0].notes, "notes");

    att->set_virtual(attendees[0].id, false);
    auto updated = att->find_by_entity("meeting", meeting_id);
    EXPECT_FALSE(updated[0].is_virtual);
}

TEST_F(AttendanceRepoTest, RemoveById) {
    att->check_in(member_id, "meeting", meeting_id);
    auto attendees = att->find_by_entity("meeting", meeting_id);
    ASSERT_EQ(attendees.size(), 1);

    att->remove_by_id(attendees[0].id);
    EXPECT_EQ(att->count_by_entity("meeting", meeting_id), 0);
}

TEST_F(AttendanceRepoTest, DeleteByEntity) {
    // Add a second member
    Member m2;
    m2.discord_user_id = "att-test-2";
    m2.discord_username = "att2";
    m2.display_name = "Second";
    m2.role = "member";
    auto m2_id = members->create(m2).id;

    att->check_in(member_id, "meeting", meeting_id);
    att->check_in(m2_id, "meeting", meeting_id);
    EXPECT_EQ(att->count_by_entity("meeting", meeting_id), 2);

    att->delete_by_entity("meeting", meeting_id);
    EXPECT_EQ(att->count_by_entity("meeting", meeting_id), 0);
}

TEST_F(AttendanceRepoTest, MemberHistory) {
    att->check_in(member_id, "meeting", meeting_id);
    auto history = att->find_by_member(member_id);
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history[0].entity_type, "meeting");
}

// ═══════════════════════════════════════════════════════════════════════════
// Chapter Member Repository
// ═══════════════════════════════════════════════════════════════════════════

class ChapterMemberRepoTest : public DbFixture {
protected:
    std::unique_ptr<ChapterMemberRepository> repo;
    std::unique_ptr<ChapterRepository> chapters;
    std::unique_ptr<MemberRepository> members;
    int64_t chapter_id = 0;
    int64_t member_id = 0;

    void SetUp() override {
        DbFixture::SetUp();
        repo = std::make_unique<ChapterMemberRepository>(*db);
        chapters = std::make_unique<ChapterRepository>(*db);
        members = std::make_unique<MemberRepository>(*db);

        Chapter ch;
        ch.name = "CM Test Chapter";
        chapter_id = chapters->create(ch).id;

        Member m;
        m.discord_user_id = "cm-test";
        m.discord_username = "cmtest";
        m.display_name = "CM Tester";
        m.role = "member";
        member_id = members->create(m).id;
    }
};

TEST_F(ChapterMemberRepoTest, UpsertAndGetRole) {
    repo->upsert(member_id, chapter_id, "member", 0);
    auto role = repo->get_chapter_role(member_id, chapter_id);
    ASSERT_TRUE(role.has_value());
    EXPECT_EQ(*role, "member");
}

TEST_F(ChapterMemberRepoTest, PromoteToLead) {
    repo->upsert(member_id, chapter_id, "member", 0);
    repo->upsert(member_id, chapter_id, "lead", 0);
    auto role = repo->get_chapter_role(member_id, chapter_id);
    EXPECT_EQ(*role, "lead");
}

TEST_F(ChapterMemberRepoTest, FindByChapter) {
    repo->upsert(member_id, chapter_id, "lead", 0);
    auto members_list = repo->find_by_chapter(chapter_id);
    ASSERT_EQ(members_list.size(), 1);
    EXPECT_EQ(members_list[0].chapter_role, "lead");
}

TEST_F(ChapterMemberRepoTest, FindByMember) {
    repo->upsert(member_id, chapter_id, "member", 0);
    auto memberships = repo->find_by_member(member_id);
    ASSERT_EQ(memberships.size(), 1);
    EXPECT_EQ(memberships[0].chapter_id, chapter_id);
}
