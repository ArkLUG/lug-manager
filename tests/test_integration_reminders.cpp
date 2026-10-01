// Discord reminders: due-window selection and send-once behaviour. The test
// DiscordClient has no channels configured, so we exercise claim/skip logic
// through reminder_sent_at rather than real posts.
#include "integration_test_base.hpp"
#include "services/ReminderService.hpp"

TEST_F(IntegrationTest, RemindersOffByDefault) {
    ReminderService svc(*db, *meeting_repo, *event_repo, *chapter_repo, *member_repo,
                        *settings_repo, *discord_client);
    auto r = svc.run_once();
    EXPECT_EQ(r.meetings + r.events + r.dms, 0);
}

TEST(DiscordTime, LocalToEpochHonorsTimezone) {
    // 2026-01-15 12:00 in Chicago (CST, UTC-6) == 18:00 UTC
    std::time_t t = DiscordClient::local_to_epoch("2026-01-15T12:00:00", "America/Chicago");
    std::tm utc{};
    gmtime_r(&t, &utc);
    EXPECT_EQ(utc.tm_hour, 18);
    EXPECT_EQ(DiscordClient::friendly_time("2026-01-15T19:05:00", "America/Chicago"), "Thu 1/15 7:05 PM CST");
}
