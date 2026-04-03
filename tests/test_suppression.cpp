#include <gtest/gtest.h>
#include "test_helper.hpp"
#include "integrations/DiscordClient.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/MeetingRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "models/LugEvent.hpp"
#include "models/Meeting.hpp"
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════════════
// Announcement Content Builder Tests (suppress_pings)
// ═══════════════════════════════════════════════════════════════════════════

TEST(AnnouncementSuppression, EventPingsPresent) {
    LugEvent e;
    e.title = "Test Event";
    e.start_time = "2026-06-15";
    e.end_time = "2026-06-17";
    e.location = "Convention Center";

    auto content = DiscordClient::build_event_announcement_content(e, "123456", "", false);
    EXPECT_NE(content.find("<@&123456>"), std::string::npos);
}

TEST(AnnouncementSuppression, EventPingsSuppressed) {
    LugEvent e;
    e.title = "Test Event";
    e.start_time = "2026-06-15";
    e.end_time = "2026-06-17";

    auto content = DiscordClient::build_event_announcement_content(e, "123456", "", true);
    EXPECT_EQ(content.find("<@&"), std::string::npos);
}

TEST(AnnouncementSuppression, MeetingPingsPresent) {
    Meeting m;
    m.title = "Test Meeting";
    m.start_time = "2026-04-15T19:00:00";
    m.end_time = "2026-04-15T21:00:00";

    auto content = DiscordClient::build_meeting_announcement_content(m, "789012", "UTC", false);
    EXPECT_NE(content.find("<@&789012>"), std::string::npos);
}

TEST(AnnouncementSuppression, MeetingPingsSuppressed) {
    Meeting m;
    m.title = "Test Meeting";
    m.start_time = "2026-04-15T19:00:00";
    m.end_time = "2026-04-15T21:00:00";

    auto content = DiscordClient::build_meeting_announcement_content(m, "789012", "UTC", true);
    EXPECT_EQ(content.find("<@&"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Per-Entity Suppress Flag Tests (database persistence)
// ═══════════════════════════════════════════════════════════════════════════

class SuppressFlagTest : public DbFixture {
protected:
    std::unique_ptr<EventRepository> events;
    std::unique_ptr<MeetingRepository> meetings;
    void SetUp() override {
        DbFixture::SetUp();
        events = std::make_unique<EventRepository>(*db);
        meetings = std::make_unique<MeetingRepository>(*db);
    }
};

TEST_F(SuppressFlagTest, EventSuppressFlagsPersist) {
    LugEvent e;
    e.title = "Historical Event";
    e.start_time = "2025-01-15";
    e.end_time = "2025-01-16";
    e.ical_uid = "suppress-test-001";
    e.suppress_discord = true;
    e.suppress_calendar = true;
    auto created = events->create(e);

    auto found = events->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->suppress_discord);
    EXPECT_TRUE(found->suppress_calendar);
}

TEST_F(SuppressFlagTest, EventDefaultNotSuppressed) {
    LugEvent e;
    e.title = "Normal Event";
    e.start_time = "2026-06-15";
    e.end_time = "2026-06-16";
    e.ical_uid = "suppress-test-002";
    auto created = events->create(e);

    auto found = events->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->suppress_discord);
    EXPECT_FALSE(found->suppress_calendar);
}

TEST_F(SuppressFlagTest, MeetingSuppressFlagsPersist) {
    Meeting m;
    m.title = "Historical Meeting";
    m.start_time = "2025-01-15T19:00:00";
    m.end_time = "2025-01-15T21:00:00";
    m.ical_uid = "suppress-test-003";
    m.suppress_discord = true;
    m.suppress_calendar = true;
    auto created = meetings->create(m);

    auto found = meetings->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->suppress_discord);
    EXPECT_TRUE(found->suppress_calendar);
}

TEST_F(SuppressFlagTest, EventNotesPersist) {
    LugEvent e;
    e.title = "Event With Notes";
    e.start_time = "2026-06-15";
    e.end_time = "2026-06-16";
    e.ical_uid = "notes-test-001";
    e.notes = "## Summary\nGreat event, 50 attendees.";
    auto created = events->create(e);

    auto found = events->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->notes, "## Summary\nGreat event, 50 attendees.");
}

TEST_F(SuppressFlagTest, MeetingNotesPersist) {
    Meeting m;
    m.title = "Meeting With Notes";
    m.start_time = "2026-04-15T19:00:00";
    m.end_time = "2026-04-15T21:00:00";
    m.ical_uid = "notes-test-002";
    m.notes = "- Action item 1\n- Action item 2";
    auto created = meetings->create(m);

    auto found = meetings->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->notes, "- Action item 1\n- Action item 2");
}

TEST_F(SuppressFlagTest, EventSuppressFlagsUpdate) {
    LugEvent e;
    e.title = "Toggle Suppress";
    e.start_time = "2026-06-15";
    e.end_time = "2026-06-16";
    e.ical_uid = "suppress-update-001";
    e.suppress_discord = false;
    auto created = events->create(e);
    EXPECT_FALSE(created.suppress_discord);

    created.suppress_discord = true;
    created.suppress_calendar = true;
    events->update(created);

    auto found = events->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->suppress_discord);
    EXPECT_TRUE(found->suppress_calendar);
}

TEST_F(SuppressFlagTest, EventNotesDiscordPostIdUpdate) {
    LugEvent e;
    e.title = "Report Test";
    e.start_time = "2026-06-15";
    e.end_time = "2026-06-16";
    e.ical_uid = "post-id-001";
    auto created = events->create(e);
    EXPECT_EQ(created.notes_discord_post_id, "");

    events->update_notes_discord_post_id(created.id, "thread-12345");
    auto found = events->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->notes_discord_post_id, "thread-12345");
}

TEST_F(SuppressFlagTest, MeetingNotesDiscordPostIdUpdate) {
    Meeting m;
    m.title = "Report Test Meeting";
    m.start_time = "2026-04-15T19:00:00";
    m.end_time = "2026-04-15T21:00:00";
    m.ical_uid = "post-id-002";
    auto created = meetings->create(m);
    EXPECT_EQ(created.notes_discord_post_id, "");

    meetings->update_notes_discord_post_id(created.id, "thread-67890");
    auto found = meetings->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->notes_discord_post_id, "thread-67890");
}

// ═══════════════════════════════════════════════════════════════════════════
// Attendance Summary Tests
// ═══════════════════════════════════════════════════════════════════════════

class AttendanceSummaryTest : public DbFixture {
protected:
    std::unique_ptr<AttendanceRepository> attendance;
    std::unique_ptr<MemberRepository> members;

    void SetUp() override {
        DbFixture::SetUp();
        attendance = std::make_unique<AttendanceRepository>(*db);
        members = std::make_unique<MemberRepository>(*db);
    }
};

TEST_F(AttendanceSummaryTest, SummaryCountsCorrect) {
    // Create two members
    Member m1;
    m1.discord_user_id = "summary1";
    m1.discord_username = "user1";
    m1.display_name = "User 1";
    auto member1 = members->create(m1);

    Member m2;
    m2.discord_user_id = "summary2";
    m2.discord_username = "user2";
    m2.display_name = "User 2";
    auto member2 = members->create(m2);

    // Create meetings and events
    db->execute("INSERT INTO meetings (title, start_time, end_time, ical_uid, scope) "
                "VALUES ('Mtg 1', '2026-03-01T19:00:00', '2026-03-01T21:00:00', 'sum-m1', 'chapter')");
    db->execute("INSERT INTO meetings (title, start_time, end_time, ical_uid, scope) "
                "VALUES ('Mtg 2', '2026-04-01T19:00:00', '2026-04-01T21:00:00', 'sum-m2', 'chapter')");
    db->execute("INSERT INTO lug_events (title, start_time, end_time, ical_uid, scope, status) "
                "VALUES ('Evt 1', '2026-05-01', '2026-05-02', 'sum-e1', 'chapter', 'confirmed')");

    // Member 1 attends 2 meetings (1 virtual) and 1 event
    attendance->check_in(member1.id, "meeting", 1);
    attendance->check_in(member1.id, "meeting", 2, "", true); // virtual
    attendance->check_in(member1.id, "event", 1);

    // Member 2 attends 1 meeting
    attendance->check_in(member2.id, "meeting", 1);

    auto summaries = attendance->get_all_member_summaries();
    ASSERT_GE(summaries.size(), 2u);

    // Find member1's summary
    auto it1 = std::find_if(summaries.begin(), summaries.end(),
        [&](const auto& s) { return s.member_id == member1.id; });
    ASSERT_NE(it1, summaries.end());
    EXPECT_EQ(it1->meeting_count, 2);
    EXPECT_EQ(it1->meeting_virtual_count, 1);
    EXPECT_EQ(it1->event_count, 1);

    // Find member2's summary
    auto it2 = std::find_if(summaries.begin(), summaries.end(),
        [&](const auto& s) { return s.member_id == member2.id; });
    ASSERT_NE(it2, summaries.end());
    EXPECT_EQ(it2->meeting_count, 1);
    EXPECT_EQ(it2->meeting_virtual_count, 0);
    EXPECT_EQ(it2->event_count, 0);
}
