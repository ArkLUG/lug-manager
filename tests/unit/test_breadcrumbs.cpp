#include "utils/web/Breadcrumbs.hpp"
#include <gtest/gtest.h>

namespace {
std::string labels(const std::vector<Crumb>& t) {
    std::string s;
    for (const auto& c : t) s += (s.empty() ? "" : " > ") + c.label + (c.href.empty() ? "" : "(" + c.href + ")");
    return s;
}
}

TEST(Breadcrumbs, SettingsPagesShowTheirGroup) {
    EXPECT_EQ(labels(breadcrumbs_for("/settings/dues", "Dues settings", true)),
              "Settings(/settings/overview) > Members and money > Dues");
    EXPECT_EQ(labels(breadcrumbs_for("/perks?year=2026", "Perk Levels", true)),
              "Settings(/settings/overview) > Members and money > Perk levels");
    // "/settings" (Discord) matches only itself
    EXPECT_EQ(labels(breadcrumbs_for("/settings", "Settings", true)),
              "Settings(/settings/overview) > Discord > Discord");
    EXPECT_EQ(labels(breadcrumbs_for("/settings/messages/event_announce", "Event announcement", true)),
              "Settings(/settings/overview) > Messages > Message wording(/settings/messages) > Event announcement");
    // A POST below a settings page stops at the page itself
    EXPECT_EQ(labels(breadcrumbs_for("/perks/3/edit", "", true)),
              "Settings(/settings/overview) > Members and money > Perk levels");
}

TEST(Breadcrumbs, NoSettingsTrailForNonAdminsOrTheAuditLog) {
    EXPECT_TRUE(breadcrumbs_for("/settings/discord-matches", "Discord Matches", false).empty());
    EXPECT_TRUE(breadcrumbs_for("/audit", "Audit Log", true).empty());
    EXPECT_TRUE(breadcrumbs_for("/fancolab", "LEGO Fan CoLab", true).empty());
    EXPECT_TRUE(breadcrumbs_for("/settings/overview", "Settings", true).empty());
}

TEST(Breadcrumbs, DetailPagesLeadBackToTheirSection) {
    EXPECT_EQ(labels(breadcrumbs_for("/meetings", "Meetings", false)), "Schedule(/schedule) > Meetings");
    EXPECT_EQ(labels(breadcrumbs_for("/events/12", "Brickworld", false)),
              "Schedule(/schedule) > Events(/events) > Brickworld");
    EXPECT_EQ(labels(breadcrumbs_for("/meetings/series", "Recurring Meetings", false)),
              "Schedule(/schedule) > Meetings(/meetings) > Recurring Meetings");
    EXPECT_EQ(labels(breadcrumbs_for("/chapters/4", "NWA", false)), "Chapters(/chapters) > NWA");
    EXPECT_EQ(labels(breadcrumbs_for("/challenges/2", "Spooky builds", false)),
              "Build Challenges(/challenges) > Spooky builds");
    EXPECT_EQ(labels(breadcrumbs_for("/account/security", "Password & two-factor", false)),
              "My Account(/account) > Password & two-factor");
    EXPECT_EQ(labels(breadcrumbs_for("/chapters/4/members", "NWA — Members", false)),
              "Chapters(/chapters) > NWA(/chapters/4) > Members");
    EXPECT_EQ(labels(breadcrumbs_for("/events/all", "All events", false)),
              "Schedule(/schedule) > Events(/events) > All events");
    // Top-level pages need no trail
    EXPECT_TRUE(breadcrumbs_for("/chapters", "Chapters", false).empty());
    EXPECT_TRUE(breadcrumbs_for("/dashboard", "Dashboard", false).empty());
    EXPECT_TRUE(breadcrumbs_for("/chapters/new", "New", false).empty());
}

TEST(Breadcrumbs, HtmlEscapesAndMarksTheCurrentPage) {
    auto h = breadcrumb_html({{"Chapters", "/chapters"}, {"<b>NWA</b>", ""}});
    EXPECT_NE(h.find("aria-label=\"Breadcrumb\""), std::string::npos);
    EXPECT_NE(h.find("hx-get=\"/chapters\""), std::string::npos);
    EXPECT_NE(h.find("aria-current=\"page\""), std::string::npos);
    EXPECT_NE(h.find("&lt;b&gt;NWA&lt;/b&gt;"), std::string::npos);
    EXPECT_EQ(h.find("<b>"), std::string::npos);
    EXPECT_EQ(breadcrumb_html({}), "");
}
