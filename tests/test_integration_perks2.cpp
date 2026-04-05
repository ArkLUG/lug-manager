#include "integration_test_base.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Perk Routes — additional coverage
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, PerkCreateAndListByYear) {
    // Create a perk level for year 2025
    auto r = POST_HTMX("/perks",
        "name=Bronze&description=First+tier"
        "&meeting_attendance_required=2"
        "&event_attendance_required=1"
        "&sort_order=1"
        "&year=2025",
        admin_token);
    EXPECT_EQ(r.code, 200);

    // Verify it was created in the repo
    auto levels = perk_level_repo->find_by_year(2025);
    ASSERT_GE(levels.size(), 1u);
    bool found = false;
    for (auto& l : levels) {
        if (l.name == "Bronze") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(IntegrationTest, PerkYearSelector) {
    // Create perks for year 2025
    PerkLevel p;
    p.name = "Silver"; p.year = 2025; p.sort_order = 1;
    p.meeting_attendance_required = 3; p.event_attendance_required = 1;
    perk_level_repo->create(p);

    // GET /perks?year=2025 should show Silver
    auto r = GET("/perks?year=2025", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Silver");
}

TEST_F(IntegrationTest, PerkEditFormLoadsAdmin) {
    // Create a perk level to edit
    PerkLevel p;
    p.name = "EditMe"; p.year = 2026; p.sort_order = 5;
    p.meeting_attendance_required = 4; p.event_attendance_required = 2;
    perk_level_repo->create(p);

    auto levels = perk_level_repo->find_by_year(2026);
    ASSERT_FALSE(levels.empty());
    int64_t perk_id = 0;
    for (auto& l : levels) {
        if (l.name == "EditMe") { perk_id = l.id; break; }
    }
    ASSERT_GT(perk_id, 0);

    auto r = GET("/perks/" + std::to_string(perk_id) + "/edit", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Edit Perk Level");
    expect_contains(r, "EditMe");
}

TEST_F(IntegrationTest, PerkEditFormNonAdminForbidden) {
    PerkLevel p;
    p.name = "Forbidden"; p.year = 2026; p.sort_order = 1;
    perk_level_repo->create(p);

    auto levels = perk_level_repo->find_by_year(2026);
    int64_t perk_id = 0;
    for (auto& l : levels) {
        if (l.name == "Forbidden") { perk_id = l.id; break; }
    }
    ASSERT_GT(perk_id, 0);

    auto r = GET("/perks/" + std::to_string(perk_id) + "/edit", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, PerkUpdateViaput) {
    PerkLevel p;
    p.name = "UpdateMe"; p.year = 2026; p.sort_order = 2;
    p.meeting_attendance_required = 1; p.event_attendance_required = 0;
    perk_level_repo->create(p);

    auto levels = perk_level_repo->find_by_year(2026);
    int64_t perk_id = 0;
    for (auto& l : levels) {
        if (l.name == "UpdateMe") { perk_id = l.id; break; }
    }
    ASSERT_GT(perk_id, 0);

    auto r = PUT("/perks/" + std::to_string(perk_id),
        "name=Updated&meeting_attendance_required=5&event_attendance_required=3&sort_order=10",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto updated = perk_level_repo->find_by_id(perk_id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->name, "Updated");
    EXPECT_EQ(updated->meeting_attendance_required, 5);
    EXPECT_EQ(updated->event_attendance_required, 3);
}

TEST_F(IntegrationTest, PerkDelete) {
    PerkLevel p;
    p.name = "DeleteMe"; p.year = 2026; p.sort_order = 99;
    perk_level_repo->create(p);

    auto levels = perk_level_repo->find_by_year(2026);
    int64_t perk_id = 0;
    for (auto& l : levels) {
        if (l.name == "DeleteMe") { perk_id = l.id; break; }
    }
    ASSERT_GT(perk_id, 0);

    auto r = DEL("/perks/" + std::to_string(perk_id), "", admin_token);
    EXPECT_EQ(r.code, 200);

    auto gone = perk_level_repo->find_by_id(perk_id);
    EXPECT_FALSE(gone.has_value());
}

TEST_F(IntegrationTest, PerkDeleteNonAdminForbidden) {
    PerkLevel p;
    p.name = "NoDelete"; p.year = 2026; p.sort_order = 1;
    perk_level_repo->create(p);

    auto levels = perk_level_repo->find_by_year(2026);
    int64_t perk_id = 0;
    for (auto& l : levels) {
        if (l.name == "NoDelete") { perk_id = l.id; break; }
    }
    ASSERT_GT(perk_id, 0);

    auto r = DEL("/perks/" + std::to_string(perk_id), "", member_token);
    EXPECT_EQ(r.code, 403);

    // Verify still exists
    auto still = perk_level_repo->find_by_id(perk_id);
    EXPECT_TRUE(still.has_value());
}

TEST_F(IntegrationTest, PerkCloneBetweenYears) {
    // Create perks for 2024
    PerkLevel p1;
    p1.name = "Clone Bronze"; p1.year = 2024; p1.sort_order = 1;
    p1.meeting_attendance_required = 2; p1.event_attendance_required = 1;
    perk_level_repo->create(p1);

    PerkLevel p2;
    p2.name = "Clone Gold"; p2.year = 2024; p2.sort_order = 2;
    p2.meeting_attendance_required = 5; p2.event_attendance_required = 3;
    perk_level_repo->create(p2);

    // Clone from 2024 to 2030 (ensure target year is empty)
    auto r = POST_HTMX("/perks/clone",
        "source_year=2024&target_year=2030",
        admin_token);
    EXPECT_EQ(r.code, 200);

    // Verify tiers were cloned
    auto cloned = perk_level_repo->find_by_year(2030);
    EXPECT_EQ(cloned.size(), 2u);
}

TEST_F(IntegrationTest, PerkCloneInvalidSameYear) {
    auto r = POST_HTMX("/perks/clone",
        "source_year=2025&target_year=2025",
        admin_token);
    EXPECT_EQ(r.code, 400);
    expect_contains(r, "Invalid");
}

TEST_F(IntegrationTest, PerkCloneTargetAlreadyHasTiers) {
    // Create a perk for year 2028
    PerkLevel p;
    p.name = "Existing"; p.year = 2028; p.sort_order = 1;
    perk_level_repo->create(p);

    // Also create source year 2027
    PerkLevel src;
    src.name = "Source"; src.year = 2027; src.sort_order = 1;
    perk_level_repo->create(src);

    // Clone 2027 -> 2028 should fail since 2028 already has tiers
    auto r = POST_HTMX("/perks/clone",
        "source_year=2027&target_year=2028",
        admin_token);
    EXPECT_EQ(r.code, 400);
    expect_contains(r, "already has");
}

TEST_F(IntegrationTest, PerkSyncRolesAdmin) {
    // sync-roles with no perk levels for current year should return a "No perk levels" message
    auto r = POST("/api/perks/sync-roles", "", admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_FALSE(r.body.empty());
}

TEST_F(IntegrationTest, PerkSyncRolesNonAdminForbidden) {
    auto r = POST("/api/perks/sync-roles", "", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, PerkListNonAdminForbidden) {
    auto r = GET("/perks", member_token);
    EXPECT_EQ(r.code, 403);
}
