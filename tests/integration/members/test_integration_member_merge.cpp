// Merging duplicate member records.
#include "integration_test_base.hpp"
#include "services/members/MemberMerge.hpp"

namespace {
int64_t make_member(MemberRepository& repo, const std::string& first, const std::string& discord = "",
                    const std::string& email = "") {
    Member m;
    m.first_name = first; m.last_name = "Dupe"; m.display_name = first + " D.";
    m.discord_user_id = discord; m.email = email; m.role = "member";
    return repo.create(m).id;
}
int64_t count(SqliteDatabase& db, const std::string& sql, int64_t id) {
    auto st = db.prepare(sql);
    st.bind(1, id);
    return st.step() ? st.col_int(0) : -1;
}
}

TEST_F(IntegrationTest, MemberMergeAdminOnly) {
    EXPECT_EQ(GET("/members/merge", chapter_lead_token).code, 403);
    EXPECT_EQ(POST("/members/merge", "keep_id=1&drop_id=2", chapter_lead_token).code, 403);
    auto r = GET("/members/merge", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Merge duplicate members");
    expect_contains(GET("/members", admin_token), "/members/merge");
    expect_not_contains(GET("/members", chapter_lead_token), "/members/merge");
}

TEST_F(IntegrationTest, MemberMergeMovesEverything) {
    int64_t keep = make_member(*member_repo, "Kim", "kim-discord");
    int64_t drop = make_member(*member_repo, "Kimberly", "", "kim@example.com");
    {
        auto u = db->prepare("UPDATE members SET paid_until='2099-12-31', is_paid=1, phone='(555) 123-4567' WHERE id=?");
        u.bind(1, drop); u.step();
    }
    // Attendance on two meetings, one shared with the kept record
    Meeting m1; m1.title = "M1"; m1.start_time = "2030-01-01T19:00:00"; m1.end_time = "2030-01-01T21:00:00";
    m1.scope = "lug_wide"; m1.status = "scheduled"; m1.suppress_discord = true; m1.suppress_calendar = true;
    auto a = meeting_svc->create(m1);
    m1.title = "M2"; auto b = meeting_svc->create(m1);
    for (auto [mid, who] : std::vector<std::pair<int64_t, int64_t>>{{a.id, drop}, {b.id, drop}, {a.id, keep}}) {
        auto st = db->prepare("INSERT INTO attendance (member_id, entity_type, entity_id) VALUES (?, 'meeting', ?)");
        st.bind(1, who); st.bind(2, mid); st.step();
    }
    {
        auto st = db->prepare("INSERT INTO dues_payments (member_id, paid_on, amount_cents, covers_until) VALUES (?, '2030-01-01', 2500, '2030-12-31')");
        st.bind(1, drop); st.step();
    }
    chapter_member_repo->upsert(drop, test_chapter_id, "lead", admin_member_id);
    chapter_member_repo->upsert(keep, test_chapter_id, "member", admin_member_id);

    // Preview shows what moves
    std::string ids = "keep_id=" + std::to_string(keep) + "&drop_id=" + std::to_string(drop);
    auto pv = POST("/members/merge/preview", ids, admin_token);
    EXPECT_EQ(pv.code, 200);
    expect_contains(pv, "event check-ins: 2");
    expect_contains(pv, "Dues payments: 1");
    expect_contains(pv, "value=\"" + std::to_string(keep) + "\"");
    EXPECT_EQ(POST("/members/merge/preview", "keep_id=" + std::to_string(keep) + "&drop_id=" + std::to_string(keep), admin_token).code, 400);

    auto r = POST("/members/merge", ids, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Merged Kimberly D. into Kim D.");

    EXPECT_FALSE(member_repo->find_by_id(drop).has_value());
    auto kept = member_repo->find_by_id(keep);
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(kept->email, "kim@example.com");          // filled from duplicate
    EXPECT_EQ(kept->first_name, "Kim");                 // own value kept
    EXPECT_EQ(kept->discord_user_id, "kim-discord");
    EXPECT_EQ(kept->paid_until, "2099-12-31");
    EXPECT_EQ(count(*db, "SELECT COUNT(*) FROM attendance WHERE member_id=?", keep), 2);   // shared one de-duplicated
    EXPECT_EQ(count(*db, "SELECT COUNT(*) FROM dues_payments WHERE member_id=?", keep), 1);
    auto role = chapter_member_repo->get_chapter_role(keep, test_chapter_id);
    ASSERT_TRUE(role.has_value());
    EXPECT_EQ(*role, "lead");                            // higher chapter role wins
    EXPECT_EQ(count(*db, "SELECT COUNT(*) FROM audit_log WHERE action='member.merge' AND entity_id=?", keep), 1);
}

TEST_F(IntegrationTest, MemberMergeRefusals) {
    int64_t a = make_member(*member_repo, "Alpha", "alpha-discord");
    int64_t b = make_member(*member_repo, "Beta", "beta-discord");
    auto r = POST("/members/merge", "keep_id=" + std::to_string(a) + "&drop_id=" + std::to_string(b), admin_token);
    EXPECT_EQ(r.code, 409);
    expect_contains(r, "different Discord accounts");
    EXPECT_TRUE(member_repo->find_by_id(b).has_value());
    // Can't merge your own record away
    EXPECT_EQ(POST("/members/merge", "keep_id=" + std::to_string(a) + "&drop_id=" + std::to_string(admin_member_id), admin_token).code, 409);
    EXPECT_EQ(POST("/members/merge", "keep_id=" + std::to_string(a) + "&drop_id=999999", admin_token).code, 404);

    // Discord link moves to the kept record when only the duplicate has one
    int64_t c = make_member(*member_repo, "Gamma");
    ASSERT_EQ(POST("/members/merge", "keep_id=" + std::to_string(c) + "&drop_id=" + std::to_string(b), admin_token).code, 200);
    EXPECT_EQ(member_repo->find_by_id(c)->discord_user_id, "beta-discord");
}
