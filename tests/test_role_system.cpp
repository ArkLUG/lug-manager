#include <gtest/gtest.h>
#include "test_helper.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "middleware/AuthMiddleware.hpp"
#include "models/Member.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Role Mapping Tests
// ═══════════════════════════════════════════════════════════════════════════

class RoleMappingTest : public DbFixture {
protected:
    std::unique_ptr<RoleMappingRepository> role_mappings;
    void SetUp() override {
        DbFixture::SetUp();
        role_mappings = std::make_unique<RoleMappingRepository>(*db);
    }
};

TEST_F(RoleMappingTest, AdminWins) {
    role_mappings->upsert("role1", "Admin Role", "admin");
    role_mappings->upsert("role2", "Member Role", "member");

    auto result = role_mappings->resolve_lug_role({"role1", "role2"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "admin");
}

TEST_F(RoleMappingTest, ChapterLeadOverMember) {
    role_mappings->upsert("role1", "Lead Role", "chapter_lead");
    role_mappings->upsert("role2", "Member Role", "member");

    auto result = role_mappings->resolve_lug_role({"role1", "role2"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "chapter_lead");
}

TEST_F(RoleMappingTest, NoMatch) {
    role_mappings->upsert("role1", "Admin Role", "admin");

    auto result = role_mappings->resolve_lug_role({"unmapped_role"});
    EXPECT_FALSE(result.has_value());
}

TEST_F(RoleMappingTest, EmptyInput) {
    auto result = role_mappings->resolve_lug_role({});
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Member Model Tests (birthday, fol_status)
// ═══════════════════════════════════════════════════════════════════════════

class MemberModelTest : public DbFixture {
protected:
    std::unique_ptr<MemberRepository> members;
    void SetUp() override {
        DbFixture::SetUp();
        members = std::make_unique<MemberRepository>(*db);
    }
};

TEST_F(MemberModelTest, DefaultRoleIsMember) {
    Member m;
    m.discord_user_id = "test001";
    m.discord_username = "testuser";
    m.display_name = "Test U.";
    auto created = members->create(m);
    EXPECT_EQ(created.role, "member");
}

TEST_F(MemberModelTest, BirthdayAndFolStatusPersist) {
    Member m;
    m.discord_user_id = "test002";
    m.discord_username = "testuser2";
    m.display_name = "Test2 U.";
    m.birthday = "2010-05-15";
    m.fol_status = "kfol";
    auto created = members->create(m);

    auto found = members->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->birthday, "2010-05-15");
    EXPECT_EQ(found->fol_status, "kfol");
}

TEST_F(MemberModelTest, DefaultFolStatusIsAfol) {
    Member m;
    m.discord_user_id = "test003";
    m.discord_username = "testuser3";
    m.display_name = "Test3 U.";
    auto created = members->create(m);

    auto found = members->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->fol_status, "afol");
}

TEST_F(MemberModelTest, ReadonlyRoleRejected) {
    // The CHECK constraint should reject 'readonly' role
    Member m;
    m.discord_user_id = "test004";
    m.discord_username = "testuser4";
    m.display_name = "Test4 U.";
    m.role = "readonly";
    EXPECT_THROW(members->create(m), std::exception);
}

TEST_F(MemberModelTest, InvalidFolStatusRejected) {
    Member m;
    m.discord_user_id = "test005";
    m.discord_username = "testuser5";
    m.display_name = "Test5 U.";
    m.fol_status = "invalid";
    EXPECT_THROW(members->create(m), std::exception);
}

TEST_F(MemberModelTest, AllFolStatusValuesAccepted) {
    for (const auto& [suffix, fol] : std::vector<std::pair<std::string, std::string>>{
        {"a", "afol"}, {"t", "tfol"}, {"k", "kfol"}}) {
        Member m;
        m.discord_user_id = "fol_test_" + suffix;
        m.discord_username = "fol_" + suffix;
        m.display_name = "FOL " + suffix;
        m.fol_status = fol;
        auto created = members->create(m);
        EXPECT_EQ(created.fol_status, fol);
    }
}

TEST_F(MemberModelTest, UpdateBirthdayAndFolStatus) {
    Member m;
    m.discord_user_id = "test006";
    m.discord_username = "testuser6";
    m.display_name = "Test6 U.";
    auto created = members->create(m);
    EXPECT_EQ(created.birthday, "");
    EXPECT_EQ(created.fol_status, "afol");

    created.birthday = "2015-08-20";
    created.fol_status = "tfol";
    members->update(created);

    auto found = members->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->birthday, "2015-08-20");
    EXPECT_EQ(found->fol_status, "tfol");
}

TEST_F(MemberModelTest, ChapterLeadRoleAccepted) {
    Member m;
    m.discord_user_id = "test007";
    m.discord_username = "testuser7";
    m.display_name = "Test7 U.";
    m.role = "chapter_lead";
    auto created = members->create(m);
    EXPECT_EQ(created.role, "chapter_lead");
}

// ═══════════════════════════════════════════════════════════════════════════
// Perk Level Tests
// ═══════════════════════════════════════════════════════════════════════════

class PerkLevelTest : public DbFixture {
protected:
    std::unique_ptr<PerkLevelRepository> perks;
    std::unique_ptr<AttendanceRepository> attendance;
    std::unique_ptr<MemberRepository> members;

    void SetUp() override {
        DbFixture::SetUp();
        perks = std::make_unique<PerkLevelRepository>(*db);
        attendance = std::make_unique<AttendanceRepository>(*db);
        members = std::make_unique<MemberRepository>(*db);
    }
};

TEST_F(PerkLevelTest, CRUD) {
    PerkLevel p;
    p.name = "Bronze";
    p.discord_role_id = "111222333";
    p.meeting_attendance_required = 3;
    p.event_attendance_required = 1;
    p.requires_paid_dues = true;
    p.sort_order = 1;
    auto created = perks->create(p);
    EXPECT_GT(created.id, 0);
    EXPECT_EQ(created.name, "Bronze");
    EXPECT_EQ(created.discord_role_id, "111222333");
    EXPECT_EQ(created.meeting_attendance_required, 3);
    EXPECT_EQ(created.event_attendance_required, 1);
    EXPECT_TRUE(created.requires_paid_dues);
    EXPECT_EQ(created.sort_order, 1);

    // Update all fields
    created.name = "Silver";
    created.discord_role_id = "444555666";
    created.meeting_attendance_required = 5;
    created.event_attendance_required = 2;
    created.requires_paid_dues = false;
    created.sort_order = 2;
    perks->update(created);
    auto found = perks->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Silver");
    EXPECT_EQ(found->discord_role_id, "444555666");
    EXPECT_EQ(found->meeting_attendance_required, 5);
    EXPECT_EQ(found->event_attendance_required, 2);
    EXPECT_FALSE(found->requires_paid_dues);
    EXPECT_EQ(found->sort_order, 2);

    perks->remove(created.id);
    EXPECT_FALSE(perks->find_by_id(created.id).has_value());
}

TEST_F(PerkLevelTest, OrderBySortOrder) {
    PerkLevel p1; p1.name = "Gold"; p1.sort_order = 3;
    PerkLevel p2; p2.name = "Bronze"; p2.sort_order = 1;
    PerkLevel p3; p3.name = "Silver"; p3.sort_order = 2;
    perks->create(p1);
    perks->create(p2);
    perks->create(p3);

    auto all = perks->find_all();
    ASSERT_EQ(all.size(), 3);
    EXPECT_EQ(all[0].name, "Bronze");
    EXPECT_EQ(all[1].name, "Silver");
    EXPECT_EQ(all[2].name, "Gold");
}

TEST_F(PerkLevelTest, AttendanceCountByYear) {
    Member m;
    m.discord_user_id = "perk_test";
    m.discord_username = "perk_user";
    m.display_name = "Perk U.";
    auto member = members->create(m);

    // Create a meeting and check in
    db->execute("INSERT INTO meetings (title, start_time, end_time, ical_uid, scope) "
                "VALUES ('Test Meeting', '2026-03-15T19:00:00', '2026-03-15T21:00:00', 'uid-001', 'chapter')");
    attendance->check_in(member.id, "meeting", 1);

    db->execute("INSERT INTO meetings (title, start_time, end_time, ical_uid, scope) "
                "VALUES ('Test Meeting 2', '2026-04-15T19:00:00', '2026-04-15T21:00:00', 'uid-002', 'chapter')");
    attendance->check_in(member.id, "meeting", 2);

    int count_2026 = attendance->count_member_by_year(member.id, 2026, "meeting");
    EXPECT_EQ(count_2026, 2);

    int count_2025 = attendance->count_member_by_year(member.id, 2025, "meeting");
    EXPECT_EQ(count_2025, 0);

    int event_count = attendance->count_member_by_year(member.id, 2026, "event");
    EXPECT_EQ(event_count, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Chapter Role Rank Tests
// ═══════════════════════════════════════════════════════════════════════════

class ChapterRoleTest : public DbFixture {
protected:
    std::unique_ptr<ChapterMemberRepository> chapter_members;
    void SetUp() override {
        DbFixture::SetUp();
        chapter_members = std::make_unique<ChapterMemberRepository>(*db);
    }
};

TEST_F(ChapterRoleTest, LeadOutranksEventManager) {
    // chapter_role_rank is defined in AuthMiddleware.hpp
    EXPECT_GT(chapter_role_rank("lead"), chapter_role_rank("event_manager"));
    EXPECT_GT(chapter_role_rank("event_manager"), chapter_role_rank("member"));
}
