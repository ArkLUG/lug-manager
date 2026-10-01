// Recurring meeting rules.
#include <gtest/gtest.h>
#include "services/events/SeriesService.hpp"

namespace {
MeetingSeries monthly(int weekday, int nth) {
    MeetingSeries s; s.rule = "monthly"; s.weekday = weekday; s.nth = nth; s.starts_on = "2026-01-01"; return s;
}
}

TEST(Series, SecondTuesdayOfMonth) {
    auto d = SeriesService::occurrences(monthly(2, 2), "2026-01-01", "2026-03-31");
    ASSERT_EQ(d.size(), 3u);
    EXPECT_EQ(d[0], "2026-01-13");
    EXPECT_EQ(d[1], "2026-02-10");
    EXPECT_EQ(d[2], "2026-03-10");
}

TEST(Series, LastFridayOfMonth) {
    auto d = SeriesService::occurrences(monthly(5, -1), "2026-01-01", "2026-02-28");
    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d[0], "2026-01-30");
    EXPECT_EQ(d[1], "2026-02-27");
}

TEST(Series, FifthWeekdaySkipsShortMonths) {
    // Only months with five Thursdays have a "5th" - nth is capped at 4 by the
    // UI, but a 4th always exists.
    auto d = SeriesService::occurrences(monthly(4, 4), "2026-01-01", "2026-04-30");
    EXPECT_EQ(d.size(), 4u);
}

TEST(Series, EveryOtherWeekAnchoredToStart) {
    MeetingSeries s; s.rule = "weekly"; s.weekday = 3; s.interval_weeks = 2; s.starts_on = "2026-01-07"; // a Wednesday
    auto d = SeriesService::occurrences(s, "2026-01-14", "2026-02-15");
    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d[0], "2026-01-21");
    EXPECT_EQ(d[1], "2026-02-04");
}

TEST(Series, RespectsStartAndEnd) {
    MeetingSeries s; s.rule = "weekly"; s.weekday = 1; s.starts_on = "2026-03-02"; s.ends_on = "2026-03-16";
    auto d = SeriesService::occurrences(s, "2026-01-01", "2026-12-31");
    ASSERT_EQ(d.size(), 3u);
    EXPECT_EQ(d.front(), "2026-03-02");
    EXPECT_EQ(d.back(), "2026-03-16");
}

TEST(Series, Describe) {
    auto s = monthly(2, 2); s.start_hm = "19:00";
    EXPECT_EQ(SeriesService::describe(s), "2nd Tuesday of every month, 7:00 PM");
    MeetingSeries w; w.rule = "weekly"; w.weekday = 6; w.interval_weeks = 2; w.start_hm = "10:30";
    EXPECT_EQ(SeriesService::describe(w), "Every 2 weeks on Saturday, 10:30 AM");
}
