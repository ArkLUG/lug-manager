// Money invariants (Aaron asked for coverage, 2026-10-12):
//  - the LUG's bucket plus every chapter's always add up to the LUG-wide
//    figures, through share changes, chapterless members, edits, deletes and
//    members moving chapter;
//  - a dues payment, once recorded, keeps its chapter and share for good.
#include "integration_test_base.hpp"
#include "services/ChapterMoney.hpp"
#include <map>

namespace {
// LUG-wide net for [from, to) and everything before `from`, straight from the tables.
int64_t lug_wide(SqliteDatabase& db, const std::string& from, const std::string& to) {
    return query_int(db, "SELECT COALESCE(SUM(amount_cents),0) FROM dues_payments WHERE paid_on >= ? AND paid_on < ?", from, to)
         + query_int(db, "SELECT COALESCE(SUM(CASE kind WHEN 'income' THEN amount_cents ELSE -amount_cents END),0) "
                         "FROM treasury_entries WHERE entry_on >= ? AND entry_on < ?", from, to);
}

// Every column of every dues payment, as text
std::map<int64_t, std::string> payment_rows(SqliteDatabase& db) {
    std::map<int64_t, std::string> out;
    auto st = db.prepare("SELECT id, member_id, paid_on, amount_cents, method, covers_until, note, "
                         "COALESCE(chapter_id,'-'), COALESCE(chapter_pct,'-') FROM dues_payments");
    while (st.step()) {
        std::string row;
        for (int c = 1; c < 9; ++c) row += st.col_text(c) + "|";
        out[st.col_int(0)] = row;
    }
    return out;
}
}

class MoneyTest : public IntegrationTest {
protected:
    int64_t ch2 = 0;
    void SetUp() override {
        IntegrationTest::SetUp();
        Chapter c; c.name = "Second Chapter"; c.shorthand = "SC";
        ch2 = chapter_repo->create(c).id;
        chapter_member_repo->upsert(regular_member_id, test_chapter_id, "member", admin_member_id);
    }
    void pay(int64_t member, const std::string& on, const std::string& amount) {
        auto r = POST("/members/" + std::to_string(member) + "/dues",
                      "paid_on=" + on + "&covers_until=2031-12-31&amount=" + amount + "&method=cash", admin_token);
        ASSERT_EQ(r.code, 200) << r.body.substr(0, 300);
    }
    void entry(const std::string& on, const std::string& kind, const std::string& amount, int64_t chapter, const std::string& cat) {
        auto r = POST("/treasury", "entry_on=" + on + "&kind=" + kind + "&amount=" + amount + "&category=" + cat +
                      "&chapter_id=" + (chapter ? std::to_string(chapter) : std::string()), admin_token);
        ASSERT_EQ(r.code, 200) << r.body.substr(0, 300);
    }
    // LUG + chapters == LUG-wide, for each year's net and opening balance
    void check(const char* when) {
        SCOPED_TRACE(when);
        for (const char* y : {"2029", "2030", "2031"}) {
            const std::string from = std::string(y) + "-01-01", to = std::to_string(std::stoi(y) + 1) + "-01-01";
            int64_t net = 0, opening = 0;
            for (const auto& b : chapter_money::buckets(*db, from, to)) { net += b.net(); opening += b.opening; }
            EXPECT_EQ(net, lug_wide(*db, from, to)) << y;
            EXPECT_EQ(opening, lug_wide(*db, "0000-01-01", from)) << y;
        }
    }
};

TEST_F(MoneyTest, BucketsAlwaysAddUpToTheLugWideTotal) {
    check("empty");
    settings_repo->set("dues_chapter_share", "50");
    pay(regular_member_id, "2030-01-10", "25.01");            // odd cents: chapter 12.50, LUG 12.51
    pay(admin_member_id, "2030-01-11", "30.00");              // no chapter: all LUG
    check("first payments");
    settings_repo->set("dues_chapter_share", "33");
    pay(chapter_lead_member_id, "2030-02-01", "19.99");
    settings_repo->set("dues_chapter_share", "100");
    pay(regular_member_id, "2030-03-01", "7.77");
    settings_repo->set("dues_chapter_share", "0");
    pay(regular_member_id, "2031-01-05", "50.00");
    check("share changes");
    entry("2029-12-31", "income", "100.00", 0, "Opening");
    entry("2030-04-01", "expense", "12.34", test_chapter_id, "Snacks");
    entry("2030-04-02", "expense", "0.01", ch2, "Penny");
    entry("2030-05-01", "income", "40.00", ch2, "Raffle");
    check("entries");
    // Move an entry between buckets, then delete one
    const int64_t snacks = query_int(*db, "SELECT id FROM treasury_entries WHERE category='Snacks'");
    EXPECT_EQ(POST("/treasury/" + std::to_string(snacks), "entry_on=2030-04-01&kind=expense&amount=12.34&category=Snacks&chapter_id=" +
                   std::to_string(ch2), admin_token).code, 200);
    check("entry moved");
    const int64_t penny = query_int(*db, "SELECT id FROM treasury_entries WHERE category='Penny'");
    EXPECT_LT(POST("/treasury/" + std::to_string(penny) + "/delete", "", admin_token).code, 400);
    check("entry deleted");
    // The member moves chapter: past payments stay where they were
    chapter_member_repo->remove(regular_member_id, test_chapter_id);
    chapter_member_repo->upsert(regular_member_id, ch2, "member", admin_member_id);
    pay(regular_member_id, "2031-02-01", "10.00");
    check("member moved");
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM dues_payments WHERE member_id=? AND chapter_id=?", regular_member_id, test_chapter_id), 3);
    // A chapter deleted: its payments and entries fall back to the LUG, totals still add up
    db->execute("DELETE FROM chapter_members WHERE chapter_id=" + std::to_string(ch2));
    db->execute("DELETE FROM chapters WHERE id=" + std::to_string(ch2));
    check("chapter deleted");
}

TEST_F(MoneyTest, OldPaymentsAreNeverRewritten) {
    settings_repo->set("dues_chapter_share", "50");
    pay(regular_member_id, "2030-01-10", "25.00");
    pay(admin_member_id, "2030-01-11", "30.00");
    const auto before = payment_rows(*db);
    ASSERT_EQ(before.size(), 2u);

    settings_repo->set("dues_chapter_share", "10");                       // the setting changes
    chapter_member_repo->remove(regular_member_id, test_chapter_id);      // the member changes chapter
    chapter_member_repo->upsert(regular_member_id, ch2, "member", admin_member_id);
    chapter_member_repo->upsert(admin_member_id, ch2, "member", admin_member_id);
    pay(chapter_lead_member_id, "2030-02-01", "20.00");                   // new payments arrive
    db->execute("UPDATE dues_payments SET note='touched' WHERE id=" + std::to_string(before.rbegin()->first));
    db->execute("UPDATE dues_payments SET note='' WHERE id=" + std::to_string(before.rbegin()->first));

    auto after = payment_rows(*db);
    for (const auto& [id, row] : before) EXPECT_EQ(after[id], row) << "payment " << id;
    // The new one took today's chapter and share
    EXPECT_EQ(query_int(*db, "SELECT chapter_pct FROM dues_payments WHERE member_id=?", chapter_lead_member_id), 10);

    // Editing a payment's amount/date on the member's Dues panel keeps its split
    const int64_t first = before.begin()->first;
    EXPECT_EQ(POST("/members/" + std::to_string(regular_member_id) + "/dues/" + std::to_string(first),
                   "paid_on=2030-01-12&covers_until=2031-12-31&amount=26.00&method=card", admin_token).code, 200);
    EXPECT_EQ(query_int(*db, "SELECT chapter_pct FROM dues_payments WHERE id=?", first), 50);
    EXPECT_EQ(query_int(*db, "SELECT chapter_id FROM dues_payments WHERE id=?", first), test_chapter_id);

    // Merging a duplicate into another member keeps each payment's split
    Member dup; dup.first_name = "Dup"; dup.last_name = "Licate"; dup.display_name = "Dup L.";
    const int64_t d = member_repo->create(dup).id;
    chapter_member_repo->upsert(d, ch2, "member", admin_member_id);
    pay(d, "2030-03-03", "15.00");
    const int64_t dp = query_int(*db, "SELECT id FROM dues_payments WHERE member_id=?", d);
    auto m = POST("/members/merge", "keep_id=" + std::to_string(regular_member_id) + "&drop_id=" + std::to_string(d), admin_token);
    EXPECT_LT(m.code, 400) << m.body.substr(0, 200);
    EXPECT_EQ(query_int(*db, "SELECT member_id FROM dues_payments WHERE id=?", dp), regular_member_id);
    EXPECT_EQ(query_int(*db, "SELECT chapter_id FROM dues_payments WHERE id=?", dp), ch2);
    EXPECT_EQ(query_int(*db, "SELECT chapter_pct FROM dues_payments WHERE id=?", dp), 10);
}
