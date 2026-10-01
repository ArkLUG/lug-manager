// Treasury: income/expenses + dues, yearly totals.
#include "integration_test_base.hpp"
#include <filesystem>

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


TEST_F(IntegrationTest, TreasurerRole) {
    EXPECT_EQ(GET("/treasury", member_token).code, 403);
    expect_not_contains(GET("/dashboard", member_token), "hx-get=\"/treasury\"");
    // Only admins can make someone treasurer (via the member edit form)
    std::string form = "first_name=Regular&last_name=User&treasurer_form=1&is_treasurer=1";
    POST("/members/" + std::to_string(regular_member_id), form, chapter_lead_token);
    EXPECT_EQ(GET("/treasury", member_token).code, 403);
    expect_contains(GET("/members/" + std::to_string(regular_member_id), admin_token), "Treasurer");
    POST("/members/" + std::to_string(regular_member_id), form, admin_token);
    EXPECT_EQ(GET("/treasury", member_token).code, 200);           // session picks it up immediately
    expect_contains(GET("/dashboard", member_token), "hx-get=\"/treasury\"");
    EXPECT_EQ(POST("/treasury", "entry_on=2030-01-02&kind=expense&amount=5", member_token).code, 200);
    // ...but it doesn't make them an admin
    EXPECT_EQ(GET("/settings", member_token).code == 200, false);
    POST("/members/" + std::to_string(regular_member_id), "first_name=Regular&last_name=User&treasurer_form=1", admin_token);
    EXPECT_EQ(GET("/treasury", member_token).code, 403);
    auto a = db->prepare("SELECT COUNT(*) FROM audit_log WHERE action='member.treasurer'");
    ASSERT_TRUE(a.step());
    EXPECT_EQ(a.col_int(0), 2);
}

TEST_F(IntegrationTest, TreasuryReceipts) {
    // Receipt with a new entry (multipart)
    auto r = POST_FILE("/treasury", "receipt", "r.pdf", "%PDF-1.4\n%fake receipt\n", admin_token,
                       {{"entry_on", "2030-03-03"}, {"kind", "expense"}, {"amount", "12.00"}, {"category", "Supplies"}});
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Expense of $12.00 recorded.");
    auto p = r.body.find("/treasury/receipts/");
    ASSERT_NE(p, std::string::npos);
    std::string url = r.body.substr(p, r.body.find('"', p) - p);
    auto dl = GET(url, admin_token);
    EXPECT_EQ(dl.code, 200);
    EXPECT_EQ(dl.body.substr(0, 5), "%PDF-");
    EXPECT_NE(dl.headers.find("attachment"), std::string::npos);
    EXPECT_NE(dl.headers.find("sandbox"), std::string::npos);
    EXPECT_EQ(GET(url, member_token).code, 403);
    EXPECT_EQ(GET("/uploads/" + url.substr(url.rfind('/') + 1), member_token).code, 404);   // not via the gallery route

    // Bad file types rejected; attach a photo to an entry without one
    EXPECT_EQ(POST_FILE("/treasury", "receipt", "x.svg", "<svg/>", admin_token,
                        {{"entry_on", "2030-03-03"}, {"kind", "expense"}, {"amount", "1"}}).code, 400);
    ASSERT_EQ(POST("/treasury", "entry_on=2030-03-04&kind=expense&amount=3&category=Snacks", admin_token).code, 200);
    auto q = db->prepare("SELECT id FROM treasury_entries WHERE category='Snacks'");
    ASSERT_TRUE(q.step());
    std::string id = std::to_string(q.col_int(0));
    q.reset();
    auto at = POST_FILE("/treasury/" + id + "/receipt", "receipt", "p.png", kTinyPng, admin_token);
    EXPECT_EQ(at.code, 200);
    expect_contains(at, "Receipt attached.");
    auto f = db->prepare("SELECT receipt_file FROM treasury_entries WHERE id=?");
    f.bind(1, static_cast<int64_t>(std::stoll(id)));
    ASSERT_TRUE(f.step());
    std::string file = f.col_text(0);
    f.reset();
    ASSERT_FALSE(file.empty());
    EXPECT_TRUE(std::filesystem::exists(data_dir + "/uploads/receipts/" + file));
    EXPECT_EQ(POST("/treasury/" + id + "/delete", "", admin_token).code, 200);
    EXPECT_FALSE(std::filesystem::exists(data_dir + "/uploads/receipts/" + file));
}

TEST_F(IntegrationTest, TreasuryRecordsDues) {
    std::string ok = "member_id=" + std::to_string(regular_member_id) + "&paid_on=2030-02-01&amount=25&covers_until=2030-12-31&method=cash";
    EXPECT_EQ(POST("/treasury/dues", ok, member_token).code, 403);
    EXPECT_EQ(POST("/treasury/dues", "member_id=" + std::to_string(regular_member_id) + "&paid_on=2030-02-01&amount=25&covers_until=2029-12-31", admin_token).code, 400);
    auto r = POST("/treasury/dues", ok, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Recorded $25.00 dues for Regular U.");
    expect_contains(r, "Dues - Regular U. (cash)");
    auto m = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->paid_until, "2030-12-31");
}
