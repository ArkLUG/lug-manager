#include <gtest/gtest.h>
#include "integrations/DiscordClient.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Event Announcement Content
// ═══════════════════════════════════════════════════════════════════════════

TEST(EventAnnouncement, BasicContent) {
    LugEvent e;
    e.title      = "Spring Showcase";
    e.start_time = "2026-04-15T00:00:00";
    e.end_time   = "2026-04-17T00:00:00";
    e.location   = "Convention Center";
    e.scope      = "chapter";

    auto content = DiscordClient::build_event_announcement_content(e, "role123");
    EXPECT_NE(content.find("Spring Showcase"), std::string::npos);
    EXPECT_NE(content.find("4/15"), std::string::npos);
    EXPECT_NE(content.find("Convention Center"), std::string::npos);
    EXPECT_NE(content.find("<@&role123>"), std::string::npos);
}

TEST(EventAnnouncement, SuppressPings) {
    LugEvent e;
    e.title      = "No Ping Event";
    e.start_time = "2026-04-15T00:00:00";
    e.end_time   = "2026-04-15T00:00:00";
    e.scope      = "chapter";

    auto content = DiscordClient::build_event_announcement_content(e, "role123", "", true);
    EXPECT_EQ(content.find("<@&"), std::string::npos);
    EXPECT_NE(content.find("No Ping Event"), std::string::npos);
}

TEST(EventAnnouncement, CustomPingRoles) {
    LugEvent e;
    e.title      = "Pinged Event";
    e.start_time = "2026-04-15T00:00:00";
    e.end_time   = "2026-04-15T00:00:00";
    e.scope      = "chapter";
    e.discord_ping_role_ids = "role_a,role_b";

    auto content = DiscordClient::build_event_announcement_content(e, "main_role");
    EXPECT_NE(content.find("<@&main_role>"), std::string::npos);
    EXPECT_NE(content.find("<@&role_a>"), std::string::npos);
    EXPECT_NE(content.find("<@&role_b>"), std::string::npos);
}

TEST(EventAnnouncement, NonLugPrefix) {
    LugEvent e;
    e.title      = "External Show";
    e.start_time = "2026-04-15T00:00:00";
    e.end_time   = "2026-04-15T00:00:00";
    e.scope      = "non_lug";

    auto content = DiscordClient::build_event_announcement_content(e, "");
    EXPECT_NE(content.find("[Non-LUG]"), std::string::npos);
}

TEST(EventAnnouncement, ThreadUrlIncluded) {
    LugEvent e;
    e.title      = "Thread Event";
    e.start_time = "2026-04-15T00:00:00";
    e.end_time   = "2026-04-15T00:00:00";
    e.scope      = "chapter";

    auto content = DiscordClient::build_event_announcement_content(
        e, "", "https://discord.com/channels/123/456");
    EXPECT_NE(content.find("https://discord.com/channels/123/456"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Thread Starter Content
// ═══════════════════════════════════════════════════════════════════════════

TEST(ThreadStarter, BasicContent) {
    LugEvent e;
    e.title      = "Thread Event";
    e.description = "A great event";
    e.location   = "Hall B";
    e.start_time = "2026-04-15T00:00:00";
    e.end_time   = "2026-04-17T00:00:00";
    e.scope      = "chapter";
    e.max_attendees = 50;
    e.signup_deadline = "2026-04-10T00:00:00";

    auto content = DiscordClient::build_thread_starter_content(e, "role1");
    EXPECT_NE(content.find("Thread Event"), std::string::npos);
    EXPECT_NE(content.find("Hall B"), std::string::npos);
    EXPECT_NE(content.find("A great event"), std::string::npos);
    EXPECT_NE(content.find("Capacity: 50"), std::string::npos);
    EXPECT_NE(content.find("Signup Deadline"), std::string::npos);
}

TEST(ThreadStarter, LeadMentionSuppressed) {
    LugEvent e;
    e.title      = "Lead Test";
    e.start_time = "2026-04-15T00:00:00";
    e.end_time   = "2026-04-15T00:00:00";
    e.scope      = "chapter";
    e.event_lead_discord_id = "lead123";
    e.event_lead_name = "Lead Name";

    auto with_ping = DiscordClient::build_thread_starter_content(e, false);
    EXPECT_NE(with_ping.find("<@lead123>"), std::string::npos);

    auto no_ping = DiscordClient::build_thread_starter_content(e, true);
    EXPECT_EQ(no_ping.find("<@lead123>"), std::string::npos);
    EXPECT_NE(no_ping.find("Lead Name"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Meeting Announcement Content
// ═══════════════════════════════════════════════════════════════════════════

TEST(MeetingAnnouncement, BasicContent) {
    Meeting m;
    m.title      = "Monthly Meeting";
    m.description = "Agenda items";
    m.location   = "Library Room 3";
    m.start_time = "2026-04-15T19:00:00";
    m.end_time   = "2026-04-15T21:00:00";

    auto content = DiscordClient::build_meeting_announcement_content(m, "role1", "America/Chicago");
    EXPECT_NE(content.find("Monthly Meeting"), std::string::npos);
    EXPECT_NE(content.find("Library Room 3"), std::string::npos);
    EXPECT_NE(content.find("7:00 PM"), std::string::npos);
    EXPECT_NE(content.find("Agenda items"), std::string::npos);
}

TEST(MeetingAnnouncement, SuppressPings) {
    Meeting m;
    m.title      = "Quiet Meeting";
    m.start_time = "2026-04-15T19:00:00";
    m.end_time   = "2026-04-15T21:00:00";

    auto content = DiscordClient::build_meeting_announcement_content(m, "role1", "UTC", true);
    EXPECT_EQ(content.find("<@&"), std::string::npos);
}
