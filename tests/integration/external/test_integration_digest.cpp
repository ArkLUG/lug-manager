// Weekly digest against the fake Discord (DMs) and the capturing mailer.
#include "integration_test_base.hpp"
#include "fake_discord.hpp"
#include "services/notifications/DigestService.hpp"
#include "repositories/members/NotificationPrefs.hpp"

namespace {
// A Monday at `hour`:00 local time, far in the future.
std::time_t monday_at(int hour) {
    std::tm t{}; t.tm_year = 2099 - 1900; t.tm_mon = 5; t.tm_mday = 1; t.tm_hour = hour; t.tm_isdst = -1;
    std::time_t x = std::mktime(&t);
    while (local_tm(x).tm_wday != 1) x += 86400;
    std::tm l = local_tm(x); l.tm_hour = hour; l.tm_min = 0; l.tm_sec = 0; l.tm_isdst = -1;
    return std::mktime(&l);
}
std::string iso_plus(std::time_t base, int days, int hour) {
    std::tm t = local_tm(base + days * 86400);
    char b[24]; std::snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:00:00", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, hour);
    return b;
}
}

class DigestTest : public IntegrationTest {
protected:
    std::unique_ptr<FakeDiscord> fake;
    std::time_t monday = monday_at(10);
    void SetUp() override {
        IntegrationTest::SetUp();
        fake = std::make_unique<FakeDiscord>();
        discord_client->reconfigure(fake->guild_id, "600000000000000001");
        Features::set("digest", true);
    }
    void TearDown() override { fake.reset(); IntegrationTest::TearDown(); }
    DigestService svc() { return DigestService(*db, *settings_repo, notifier_with_chat()); }
};

TEST_F(DigestTest, SendsOnceAWeekWithPersonalBits) {
    LugEvent e;
    e.title = "Library Show"; e.start_time = iso_plus(monday, 3, 10); e.end_time = iso_plus(monday, 3, 16);
    e.location = "Main Library"; e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    {
        auto r = db->prepare("INSERT INTO event_rsvps (event_id, member_id, status) VALUES (?, ?, 'going')");
        r.bind(1, ev.id); r.bind(2, regular_member_id); r.step();
    }
    int64_t shift = 0;
    {
        auto s = db->prepare("INSERT INTO event_shifts (event_id, title, starts_at, ends_at, slots) VALUES (?, 'Setup', ?, ?, 2) RETURNING id");
        s.bind(1, ev.id); s.bind(2, iso_plus(monday, 3, 8).substr(0, 16)); s.bind(3, iso_plus(monday, 3, 9).substr(0, 16));
        ASSERT_TRUE(s.step()); shift = s.col_int(0);
    }
    { auto u = db->prepare("INSERT INTO event_shift_signups (shift_id, member_id) VALUES (?, ?)"); u.bind(1, shift); u.bind(2, regular_member_id); u.step(); }
    // An email-only member whose dues run out soon
    Member em; em.first_name = "Eve"; em.last_name = "Mail"; em.display_name = "Eve M."; em.email = "eve@example.org"; em.role = "member";
    int64_t eve = member_repo->create(em).id;
    { auto u = db->prepare("UPDATE members SET is_paid=1, paid_until=? WHERE id=?"); u.bind(1, iso_plus(monday, 10, 0).substr(0, 10)); u.bind(2, eve); u.step(); }
    // The chapter lead opted out
    NotificationPrefs(*db).set(chapter_lead_member_id, "digest", false);

    auto d = svc();
    EXPECT_EQ(d.run_once(monday - 86400), 0);           // Sunday: not yet
    EXPECT_EQ(d.run_once(monday_at(8)), 0);            // Monday before 9
    int sent = d.run_once(monday);
    EXPECT_GE(sent, 3);

    ASSERT_TRUE(fake->wait_for("POST /api/v10/channels/dmmember-test-001/messages"));
    auto dm = fake->matching("POST /api/v10/channels/dmmember-test-001/messages")[0].body;
    EXPECT_NE(dm.find("Library Show"), std::string::npos);
    EXPECT_NE(dm.find("you're going"), std::string::npos);
    EXPECT_NE(dm.find("Volunteering: Setup"), std::string::npos);
    EXPECT_TRUE(fake->matching("channels/dmlead-test-001/").empty());      // opted out

    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].to, "eve@example.org");
    EXPECT_EQ(out[0].subject, "Your LUG week");
    EXPECT_NE(out[0].body.find("Your dues run out on"), std::string::npos);
    EXPECT_NE(out[0].unsubscribe_url.find("kind=digest"), std::string::npos);

    // Once per week
    fake->clear(); mailer->clear_outbox();
    EXPECT_EQ(d.run_once(monday + 3600), 0);
    EXPECT_TRUE(mailer->outbox().empty());
}

TEST_F(DigestTest, OffByDefaultAndQuietWeeksSendNothing) {
    Features::set("digest", false);
    EXPECT_EQ(svc().run_once(monday), 0);
    Features::set("digest", true);
    // Nothing on and nothing personal -> nothing sent
    EXPECT_EQ(svc().run_once(monday), 0);
    EXPECT_TRUE(fake->matching("users/@me/channels").empty());
    EXPECT_EQ(svc().build_for(regular_member_id, monday), "");
}

TEST_F(DigestTest, SwitchedOffFeaturesStayOutOfTheDigest) {
    Member em; em.first_name = "Dee"; em.last_name = "Dues"; em.display_name = "Dee D."; em.email = "dee@example.org"; em.role = "member";
    int64_t dee = member_repo->create(em).id;
    { auto u = db->prepare("UPDATE members SET is_paid=1, paid_until=? WHERE id=?"); u.bind(1, iso_plus(monday, 5, 0).substr(0, 10)); u.bind(2, dee); u.step(); }
    EXPECT_NE(svc().build_for(dee, monday).find("dues"), std::string::npos);
    Features::set("dues", false);
    EXPECT_EQ(svc().build_for(dee, monday), "");
}
