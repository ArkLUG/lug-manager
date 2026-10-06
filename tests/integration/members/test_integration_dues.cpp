// Dues ledger + expiry.
#include "integration_test_base.hpp"
#include "utils/LocalTime.hpp"
#include "services/members/DuesService.hpp"

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

TEST_F(IntegrationTest, DuesSettingsFillInTheRecordForm) {
    const std::string id = std::to_string(regular_member_id);
    // No standard amount yet: the date is still filled in
    auto empty = GET("/members/" + id + "/dues/suggest?paid_on=2026-07-02", chapter_lead_token);
    EXPECT_EQ(empty.code, 200);
    expect_contains(empty, "value=\"2026-12-31\"");
    expect_not_contains(empty, "Prorated");

    // Settings > Dues: admins only
    EXPECT_NE(GET("/settings/dues", chapter_lead_token).code, 200);
    EXPECT_EQ(GET("/settings/dues", admin_token).code, 200);
    EXPECT_EQ(POST("/settings/dues", "dues_amount=abc&dues_year_end_month=12", admin_token).code, 400);
    auto saved = POST("/settings/dues", "dues_amount=%2420&dues_year_end_month=12&dues_prorate=1", admin_token);
    EXPECT_EQ(saved.code, 200);
    expect_contains(saved, "Someone joining today");
    EXPECT_EQ(settings_repo->get("dues_amount"), "20.00");
    EXPECT_EQ(settings_repo->get("dues_prorate"), "1");

    // Joining in July: half the year
    auto jul = GET("/members/" + id + "/dues/suggest?paid_on=2026-07-02", chapter_lead_token);
    expect_contains(jul, "name=\"amount\" value=\"10.00\"");
    expect_contains(jul, "Prorated: 6 of 12 months of $20.00, rounded up");
    // The panel's form starts from today's suggestion
    auto panel = GET("/members/" + id + "/dues", chapter_lead_token);
    expect_contains(panel, "dues-suggest-" + id);
    expect_contains(panel, "/dues/suggest\"");
    // Members can't ask for suggestions
    EXPECT_NE(GET("/members/" + id + "/dues/suggest", member_token).code, 200);

    // Already paid this year: the next full year
    member_repo->set_paid(regular_member_id, true, "2026-12-31");
    auto renew = GET("/members/" + id + "/dues/suggest?paid_on=2026-11-20", chapter_lead_token);
    expect_contains(renew, "value=\"2027-12-31\"");
    expect_contains(renew, "value=\"20.00\"");

    // Proration off; a June year end
    POST("/settings/dues", "dues_amount=24&dues_year_end_month=6", admin_token);
    EXPECT_EQ(settings_repo->get("dues_prorate"), "0");
    member_repo->set_paid(regular_member_id, false, "");
    auto june = GET("/members/" + id + "/dues/suggest?paid_on=2026-08-01", chapter_lead_token);
    expect_contains(june, "value=\"2027-06-30\"");
    expect_contains(june, "value=\"24.00\"");
}

TEST_F(IntegrationTest, DuesBackfillForMembersMarkedPaid) {
    // Two marked paid with no payment, one with a payment on record, one unpaid
    member_repo->set_paid(regular_member_id, true, "2026-12-31");
    member_repo->set_paid(chapter_lead_member_id, true, "2027-06-30");
    member_repo->set_paid(event_manager_member_id, true, "2026-12-31");
    POST("/members/" + std::to_string(event_manager_member_id) + "/dues",
         "paid_on=2026-01-05&covers_until=2026-12-31&amount=20", admin_token);
    member_repo->set_paid(admin_member_id, false, "");

    auto page = GET("/settings/dues", admin_token);
    expect_contains(page, "Record 2 payments");
    EXPECT_NE(POST("/settings/dues/backfill", "amount=20&paid_on=2026-03-01", chapter_lead_token).code, 200);
    EXPECT_EQ(POST("/settings/dues/backfill", "amount=&paid_on=2026-03-01", admin_token).code, 400);

    auto r = POST("/settings/dues/backfill", "amount=%2420&paid_on=2026-03-01&method=cash", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Recorded 2 payments.");
    expect_not_contains(r, "Record past payments");                     // nothing left to backfill
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM dues_payments WHERE note='Recorded afterwards (backfill)' AND amount_cents=2000 AND paid_on='2026-03-01'"), 2);
    // Covers to each member's own paid-until, which doesn't change
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM dues_payments WHERE member_id=? AND covers_until='2027-06-30'", chapter_lead_member_id), 1);
    EXPECT_EQ(member_repo->find_by_id(chapter_lead_member_id)->paid_until, "2027-06-30");
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM dues_payments WHERE member_id=?", event_manager_member_id), 1);
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM dues_payments WHERE member_id=?", admin_member_id), 0);
    // Running it again does nothing
    POST("/settings/dues/backfill", "amount=20&paid_on=2026-03-01", admin_token);
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM dues_payments"), 3);
}

TEST_F(IntegrationTest, DuesGracePeriod) {
    // Ran out 10 days ago; with no grace the daily check marks them unpaid
    const std::time_t now = std::time(nullptr);
    const std::string ran_out = DuesService::ymd(now - 10 * 86400);
    member_repo->set_paid(regular_member_id, true, ran_out);
    DuesRepository dues(*db);
    DuesService svc(dues, *settings_repo, *discord_client, *audit_svc);

    // 30-day grace: still paid, and the panel says so
    EXPECT_EQ(POST("/settings/dues", "dues_amount=20&dues_year_end_month=12&dues_prorate=1&dues_grace_days=30", admin_token).code, 200);
    EXPECT_EQ(settings_repo->get("dues_grace_days"), "30");
    svc.run_once(now);
    EXPECT_TRUE(member_repo->find_by_id(regular_member_id)->is_paid);
    auto panel = GET("/members/" + std::to_string(regular_member_id) + "/dues", chapter_lead_token);
    expect_contains(panel, "Grace period");
    expect_contains(panel, "still counted as paid until " + friendly_date(DuesService::ymd(now - 10 * 86400 + 30 * 86400)));

    // 5-day grace: past it, so they lapse
    POST("/settings/dues", "dues_amount=20&dues_year_end_month=12&dues_prorate=1&dues_grace_days=5", admin_token);
    svc.run_once(now);
    EXPECT_FALSE(member_repo->find_by_id(regular_member_id)->is_paid);
    expect_contains(GET("/members/" + std::to_string(regular_member_id) + "/dues", chapter_lead_token), "Lapsed: paid until " + friendly_date(ran_out));
    // Junk values fall back to 0
    POST("/settings/dues", "dues_amount=20&dues_year_end_month=12&dues_grace_days=-4", admin_token);
    EXPECT_EQ(settings_repo->get("dues_grace_days"), "0");
}

TEST_F(IntegrationTest, EditAndDeleteDuesPayments) {
    const std::string base = "/members/" + std::to_string(regular_member_id) + "/dues";
    ASSERT_EQ(POST(base, "paid_on=2026-01-05&covers_until=2026-12-31&amount=20&method=cash", admin_token).code, 200);
    ASSERT_EQ(POST(base, "paid_on=2027-01-03&covers_until=2027-12-31&amount=20&method=cash", admin_token).code, 200);
    EXPECT_EQ(member_repo->find_by_id(regular_member_id)->paid_until, "2027-12-31");
    auto later = query_int(*db, "SELECT id FROM dues_payments WHERE covers_until='2027-12-31'");
    auto first = query_int(*db, "SELECT id FROM dues_payments WHERE covers_until='2026-12-31'");
    const std::string later_url = base + "/" + std::to_string(later);

    // Only admins/treasurers; leads can record but not edit
    EXPECT_EQ(GET(later_url + "/edit", chapter_lead_token).code, 403);
    EXPECT_EQ(POST(later_url, "paid_on=2027-01-03&covers_until=2027-06-30&amount=10", chapter_lead_token).code, 403);
    auto form = GET(later_url + "/edit", admin_token);
    EXPECT_EQ(form.code, 200);
    expect_contains(form, "Edit payment");
    expect_contains(form, "value=\"20.00\"");
    EXPECT_EQ(POST(later_url, "paid_on=bad&covers_until=2027-06-30&amount=10", admin_token).code, 400);

    // Shortening the payment that set paid_until moves paid_until with it
    auto r = POST(later_url, "paid_on=2027-01-03&covers_until=2027-06-30&amount=10.00&method=card", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Payment updated.");
    expect_contains(r, "$10.00");
    EXPECT_EQ(member_repo->find_by_id(regular_member_id)->paid_until, "2027-06-30");
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM audit_log WHERE action='member.dues_payment_edit' AND details LIKE '%covers until: 2027-12-31 -> 2027-06-30%'"), 1);
    // Editing an older payment doesn't shorten paid_until
    POST(base + "/" + std::to_string(first), "paid_on=2026-01-05&covers_until=2026-10-31&amount=20", admin_token);
    EXPECT_EQ(member_repo->find_by_id(regular_member_id)->paid_until, "2027-06-30");

    // Deleting the payment that set paid_until falls back to the remaining one
    auto d = POST(later_url + "/delete", "", admin_token);
    EXPECT_EQ(d.code, 200);
    expect_contains(d, "Paid until is now 2026-10-31.");
    EXPECT_EQ(member_repo->find_by_id(regular_member_id)->paid_until, "2026-10-31");
    // Deleting the last one leaves paid_until (nothing to fall back to) and says so
    auto last = POST(base + "/" + std::to_string(first) + "/delete", "", admin_token);
    expect_contains(last, "Paid until is unchanged");
    EXPECT_EQ(member_repo->find_by_id(regular_member_id)->paid_until, "2026-10-31");
}
