// Dues proration: the suggested amount and "covers until" for a payment.
#include <gtest/gtest.h>
#include "services/members/DuesProration.hpp"

namespace {
dues::Config cfg(int64_t cents, int end_month = 12, bool prorate = true) { return {cents, end_month, prorate}; }
}

TEST(DuesProration, CalendarYear) {
    auto jan = dues::suggest(cfg(2000), "2026-01-15", "");
    EXPECT_EQ(jan.cents, 2000);
    EXPECT_EQ(jan.months, 12);
    EXPECT_EQ(jan.covers_until, "2026-12-31");
    EXPECT_EQ(jan.explain, "Full year");

    auto jul = dues::suggest(cfg(2000), "2026-07-02", "");           // Jul-Dec: 6 months
    EXPECT_EQ(jul.months, 6);
    EXPECT_EQ(jul.cents, 1000);
    EXPECT_EQ(jul.covers_until, "2026-12-31");
    EXPECT_EQ(jul.explain, "Prorated: 6 of 12 months of $20.00, rounded up");

    auto aug = dues::suggest(cfg(2000), "2026-08-20", "");           // 5 months: $8.33 -> $9
    EXPECT_EQ(aug.months, 5);
    EXPECT_EQ(aug.cents, 900);
    auto dec = dues::suggest(cfg(2500), "2026-12-01", "");           // 1 month: $2.08 -> $3
    EXPECT_EQ(dec.cents, 300);
}

TEST(DuesProration, OtherYearEnds) {
    auto aug = dues::suggest(cfg(2400, 6), "2026-08-01", "");        // year ends June 30
    EXPECT_EQ(aug.covers_until, "2027-06-30");
    EXPECT_EQ(aug.months, 11);
    EXPECT_EQ(aug.cents, 2200);
    auto jun = dues::suggest(cfg(2400, 6), "2026-06-30", "");
    EXPECT_EQ(jun.covers_until, "2026-06-30");
    EXPECT_EQ(jun.months, 1);
    auto feb = dues::suggest(cfg(1200, 2), "2027-11-03", "");        // leap-year end: Feb 29, 2028
    EXPECT_EQ(feb.covers_until, "2028-02-29");
    EXPECT_EQ(feb.months, 4);
}

TEST(DuesProration, RenewalAndOptions) {
    // Already paid for this year: offered the next full year
    auto r = dues::suggest(cfg(2000), "2026-11-15", "2026-12-31");
    EXPECT_TRUE(r.renewal);
    EXPECT_EQ(r.covers_until, "2027-12-31");
    EXPECT_EQ(r.cents, 2000);
    // Paid part of the year (lapsed): prorated to the end of this one
    auto lapsed = dues::suggest(cfg(2000), "2026-07-01", "2026-03-31");
    EXPECT_FALSE(lapsed.renewal);
    EXPECT_EQ(lapsed.cents, 1000);
    // Proration off: always the full amount to the year's end
    auto off = dues::suggest(cfg(2000, 12, false), "2026-07-01", "");
    EXPECT_EQ(off.cents, 2000);
    EXPECT_EQ(off.covers_until, "2026-12-31");
    // No standard amount: just the date
    auto none = dues::suggest(cfg(0), "2026-07-01", "");
    EXPECT_EQ(none.cents, 0);
    EXPECT_EQ(none.covers_until, "2026-12-31");
    EXPECT_EQ(none.explain, "");
    EXPECT_EQ(dues::suggest(cfg(2000), "garbage", "").covers_until, "");
}
