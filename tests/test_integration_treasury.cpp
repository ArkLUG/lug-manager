// Treasury: income/expenses + dues, yearly totals.
#include "integration_test_base.hpp"

TEST_F(IntegrationTest, TreasuryAdminOnly) {
    EXPECT_NE(GET("/treasury").code, 200);
    EXPECT_EQ(GET("/treasury", member_token).code, 403);
    EXPECT_EQ(GET("/treasury", chapter_lead_token).code, 403);
    EXPECT_EQ(POST("/treasury", "entry_on=2030-01-01&kind=income&amount=10", chapter_lead_token).code, 403);
    EXPECT_EQ(GET("/treasury.csv", member_token).code, 403);
    EXPECT_EQ(GET("/treasury", admin_token).code, 200);
}

TEST_F(IntegrationTest, TreasuryTotalsAndLedger) {
    // Validation
    EXPECT_EQ(POST("/treasury", "entry_on=2030-02-01&kind=gift&amount=10", admin_token).code, 400);
    EXPECT_EQ(POST("/treasury", "entry_on=2030-02-01&kind=income&amount=-5", admin_token).code, 400);
    EXPECT_EQ(POST("/treasury", "entry_on=2030-02-01&kind=income&amount=0", admin_token).code, 400);
    EXPECT_EQ(POST("/treasury", "entry_on=bad&kind=income&amount=5", admin_token).code, 400);

    // Prior year carries into the opening balance
    ASSERT_EQ(POST("/treasury", "entry_on=2029-12-31&kind=income&category=Opening+balance&amount=100", admin_token).code, 200);
    // Dues payment in 2030 shows up as income
    {
        auto d = db->prepare("INSERT INTO dues_payments (member_id, paid_on, amount_cents, method, covers_until) VALUES (?, '2030-01-15', 2500, 'cash', '2030-12-31')");
        d.bind(1, regular_member_id);
        d.step();
    }
    LugEvent e;
    e.title = "Money Show"; e.start_time = "2030-03-01T09:00:00"; e.end_time = "2030-03-01T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    auto r = POST("/treasury", "entry_on=2030-02-10&kind=expense&category=Table+fee&amount=1%2C040.50&description=Booth&event_id=" + std::to_string(ev.id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Expense of $1,040.50 recorded.");
    ASSERT_EQ(POST("/treasury", "entry_on=2030-03-01&kind=income&category=Sponsorship&amount=200&event_id=" + std::to_string(ev.id), admin_token).code, 200);

    auto page = GET("/treasury?year=2030", admin_token);
    EXPECT_EQ(page.code, 200);
    expect_contains(page, "Treasury 2030");
    expect_contains(page, "$100.00");      // opening
    expect_contains(page, "$25.00");       // dues
    expect_contains(page, "Dues - Regular U. (cash)");
    expect_contains(page, "-$815.50");     // net = 25 + 200 - 1040.50
    expect_contains(page, "-$715.50");     // closing = 100 - 815.50
    expect_contains(page, "Money Show");
    expect_contains(page, "-$840.50");     // event net
    expect_contains(page, "<option value=\"Table fee\">");

    auto csv = GET("/treasury.csv?year=2030", admin_token);
    EXPECT_EQ(csv.code, 200);
    expect_contains(csv, "\"2030-02-10\",expense,\"Table fee\",-1040.50,\"Booth\",\"Money Show\",\"Admin U.\"");
    expect_contains(csv, "\"2030-01-15\",income,\"Dues\",25.00");
    expect_not_contains(csv, "Opening balance");

    auto rep = GET("/events/" + std::to_string(ev.id) + "/report", admin_token);
    EXPECT_EQ(rep.code, 200);
    expect_contains(rep, "Income $200.00");
    expect_contains(rep, "Expenses $1,040.50");

    auto q = db->prepare("SELECT id FROM treasury_entries WHERE category='Sponsorship'");
    ASSERT_TRUE(q.step());
    std::string id = std::to_string(q.col_int(0));
    q.reset();
    EXPECT_EQ(POST("/treasury/" + id + "/delete", "", member_token).code, 403);
    EXPECT_EQ(POST("/treasury/" + id + "/delete", "", admin_token).code, 200);
    EXPECT_EQ(POST("/treasury/" + id + "/delete", "", admin_token).code, 404);
    expect_contains(GET("/treasury?year=2030", admin_token), "-$1,015.50");
}
