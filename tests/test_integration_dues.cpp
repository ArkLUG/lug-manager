// Dues ledger + expiry.
#include "integration_test_base.hpp"
#include "services/DuesService.hpp"

TEST_F(IntegrationTest, RecordingPaymentExtendsPaidUntil) {
    auto r = POST("/members/" + std::to_string(regular_member_id) + "/dues",
                  "paid_on=2026-01-05&covers_until=2099-12-31&amount=%2425.50&method=cash", chapter_lead_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "$25.50");
    auto m = member_repo->find_by_id(regular_member_id);
    EXPECT_TRUE(m->is_paid);
    EXPECT_EQ(m->paid_until, "2099-12-31");

    // An older payment never shortens paid_until
    POST("/members/" + std::to_string(regular_member_id) + "/dues",
         "paid_on=2026-01-06&covers_until=2027-01-01", chapter_lead_token);
    EXPECT_EQ(member_repo->find_by_id(regular_member_id)->paid_until, "2099-12-31");
}

TEST_F(IntegrationTest, DuesHistoryAccess) {
    std::string url = "/members/" + std::to_string(admin_member_id) + "/dues";
    EXPECT_EQ(GET(url, member_token).code, 403);          // someone else's
    EXPECT_EQ(GET(url, admin_token).code, 200);
    EXPECT_EQ(POST(url, "covers_until=2099-01-01", member_token).code, 403);
    EXPECT_EQ(POST(url, "covers_until=not-a-date", admin_token).code, 400);
}

TEST_F(IntegrationTest, LapsedDuesAreExpired) {
    member_repo->set_paid(regular_member_id, true, "2020-01-01");
    DuesRepository dues(*db);
    DuesService svc(dues, *settings_repo, *discord_client, *audit_svc);
    auto r = svc.run_once();
    EXPECT_GE(r.expired, 1);
    EXPECT_FALSE(member_repo->find_by_id(regular_member_id)->is_paid);
}
