#include <gtest/gtest.h>
#include "services/PerkProgress.hpp"

namespace {
PerkLevel lvl(const std::string& name, int m, int e, bool dues = false, const std::string& fol = "kfol") {
    PerkLevel p;
    p.name = name;
    p.meeting_attendance_required = m;
    p.event_attendance_required = e;
    p.requires_paid_dues = dues;
    p.min_fol_status = fol;
    return p;
}
}

TEST(PerkProgress, AchievedAndNextGap) {
    std::vector<PerkLevel> levels = {lvl("Bronze", 2, 0), lvl("Silver", 4, 1), lvl("Gold", 6, 2, true)};
    auto p = compute_perk_progress(levels, 3, 0, false, "afol");
    EXPECT_EQ(p.achieved, "Bronze");
    EXPECT_EQ(p.next, "Silver");
    EXPECT_EQ(p.meetings_needed, 1);
    EXPECT_EQ(p.events_needed, 1);
    EXPECT_EQ(p.gap(), 2);
    EXPECT_FALSE(p.needs_dues);
}

TEST(PerkProgress, DuesAndAgeRequirementsReported) {
    std::vector<PerkLevel> levels = {lvl("Gold", 0, 0, true, "tfol")};
    auto p = compute_perk_progress(levels, 10, 10, false, "kfol");
    EXPECT_EQ(p.achieved, "");
    EXPECT_TRUE(p.needs_dues);
    EXPECT_EQ(p.needs_fol, "tfol");
}

TEST(PerkProgress, EverythingMet) {
    std::vector<PerkLevel> levels = {lvl("Bronze", 1, 0), lvl("Silver", 2, 0)};
    auto p = compute_perk_progress(levels, 5, 0, true, "afol");
    EXPECT_EQ(p.achieved, "Silver");
    EXPECT_EQ(p.next, "");
}
