// Email: SMTP message building, sign-in links, email notifications and
// no-login unsubscribe.
#include "integration_test_base.hpp"
#include "integrations/Mailer.hpp"
#include "services/Notifier.hpp"
#include "services/DuesService.hpp"
#include "repositories/NotificationPrefs.hpp"

namespace {
int64_t email_member(MemberRepository& repo, const std::string& email, const std::string& discord = "") {
    Member m;
    m.first_name = "Kay"; m.last_name = "Mail"; m.display_name = "Kay M.";
    m.email = email; m.discord_user_id = discord; m.role = "member";
    return repo.create(m).id;
}
std::string after(const std::string& s, const std::string& marker) {
    auto p = s.find(marker);
    if (p == std::string::npos) return "";
    p += marker.size();
    auto e = s.find_first_of(" \r\n\"?", p);
    return s.substr(p, e == std::string::npos ? std::string::npos : e - p);
}
}

TEST(Mailer, BuildsSafeMessage) {
    Mailer m(Mailer::Config{"smtp://x", "", "", "Ark LUG <lug@example.org>"});
    EXPECT_TRUE(m.enabled());
    EXPECT_EQ(Mailer::address_of("Ark LUG <lug@example.org>"), "lug@example.org");
    EXPECT_EQ(Mailer::address_of("a@b.c"), "a@b.c");
    auto raw = m.build({"kay@example.org", "Hi\r\nBcc: evil@x \xF0\x9F\x8E\x89", "line1\nline2", "https://l/unsubscribe/abc"},
                       "Thu, 01 Oct 2026 00:00:00 +0000", "<id@example.org>");
    EXPECT_EQ(raw.find("\r\nBcc:"), std::string::npos);                 // no header injection
    EXPECT_NE(raw.find("Subject: =?UTF-8?B?"), std::string::npos);       // non-ASCII encoded
    EXPECT_NE(raw.find("List-Unsubscribe: <https://l/unsubscribe/abc>\r\n"), std::string::npos);
    EXPECT_NE(raw.find("List-Unsubscribe-Post: List-Unsubscribe=One-Click\r\n"), std::string::npos);
    EXPECT_NE(raw.find("\r\n\r\nline1\r\nline2\r\n"), std::string::npos);
    EXPECT_FALSE(Mailer(Mailer::Config{}).enabled());
}

TEST_F(IntegrationTest, EmailSignInLink) {
    expect_contains(GET("/login"), "Email me a link");
    int64_t id = email_member(*member_repo, "Kay@Example.org");
    email_member(*member_repo, "discord@example.org", "has-discord-1");

    // Unknown address and Discord-linked members get the same answer and no email
    auto r = POST("/auth/email", "email=nobody%40example.org");
    EXPECT_EQ(r.code, 303);
    EXPECT_NE(r.location.find("/login?email_sent=1"), std::string::npos);
    POST("/auth/email", "email=discord%40example.org");
    EXPECT_TRUE(mailer->outbox().empty());

    POST("/auth/email", "email=kay%40EXAMPLE.org");
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].to, "Kay@Example.org");
    EXPECT_NE(out[0].unsubscribe_url.find("http://lug.test/unsubscribe/"), std::string::npos);
    std::string token = after(out[0].body, "/auth/email/");
    ASSERT_EQ(token.size(), 64u);

    // Max 3 links an hour
    for (int i = 0; i < 4; ++i) POST("/auth/email", "email=kay%40example.org");
    EXPECT_EQ(mailer->outbox().size(), 3u);

    // GET shows a confirm page (prefetch-safe); POST signs in once
    EXPECT_NE(GET("/auth/email/deadbeef").location.find("/login?error=link"), std::string::npos);
    auto confirm = GET("/auth/email/" + token);
    EXPECT_EQ(confirm.code, 200);
    expect_contains(confirm, "action=\"/auth/email/" + token + "\"");
    auto in = POST("/auth/email/" + token, "");
    EXPECT_EQ(in.code, 303);
    EXPECT_NE(in.location.find("/dashboard"), std::string::npos);
    std::string session = after(in.headers, "session=");
    session = session.substr(0, session.find(';'));
    ASSERT_FALSE(session.empty());
    EXPECT_EQ(GET("/account", session).code, 200);
    EXPECT_NE(POST("/auth/email/" + token, "").location.find("/login?error=link"), std::string::npos);   // single use
    auto a = db->prepare("SELECT COUNT(*) FROM audit_log WHERE action='auth.email_login' AND entity_id=?");
    a.bind(1, id);
    ASSERT_TRUE(a.step());
    EXPECT_EQ(a.col_int(0), 1);
}

TEST_F(IntegrationTest, EmailNotificationsAndUnsubscribe) {
    int64_t id = email_member(*member_repo, "kay@example.org");
    std::string kay = session_store->create(id, "member", "Kay M.");
    LugEvent e;
    e.title = "Full Show"; e.start_time = "2099-08-01T09:00:00"; e.end_time = "2099-08-01T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    e.max_attendees = 1;
    auto ev = event_svc->create(e);
    std::string url = "/events/" + std::to_string(ev.id) + "/rsvp";
    POST(url, "", admin_token);
    POST(url, "", kay);           // waitlisted
    POST(url, "", admin_token);   // cancel -> Kay promoted, emailed
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].subject, "You're in: Full Show");
    EXPECT_NE(out[0].body.find("Full Show"), std::string::npos);
    EXPECT_EQ(out[0].body.find("**"), std::string::npos);              // markdown stripped
    EXPECT_NE(out[0].unsubscribe_url.find("?kind=waitlist"), std::string::npos);
    std::string unsub = out[0].unsubscribe_url.substr(std::string("http://lug.test").size());

    // No-login unsubscribe page, then RFC 8058 one-click POST
    auto pg = GET(unsub);
    EXPECT_EQ(pg.code, 200);
    expect_contains(pg, "Waitlist spot opened");
    EXPECT_EQ(POST(unsub, "List-Unsubscribe=One-Click").code, 200);
    EXPECT_FALSE(NotificationPrefs(*db).wants(id, "waitlist"));
    EXPECT_TRUE(NotificationPrefs(*db).wants(id, "email"));

    mailer->clear_outbox();
    POST(url, "", kay);           // Kay cancels; admin re-RSVPs and gets bumped scenario not needed
    POST(url, "", admin_token);
    POST(url, "", kay);           // Kay waitlisted again
    POST(url, "", admin_token);   // promoted again, but opted out
    EXPECT_TRUE(mailer->outbox().empty());

    // "Stop all email"
    std::string base = unsub.substr(0, unsub.find('?'));
    EXPECT_EQ(POST(base, "kind=email").code, 200);
    EXPECT_FALSE(NotificationPrefs(*db).wants(id, "email"));
    EXPECT_EQ(POST(base, "kind=email&resubscribe=1").code, 200);
    EXPECT_TRUE(NotificationPrefs(*db).wants(id, "email"));
    EXPECT_EQ(GET("/unsubscribe/0123abcd").code, 404);
    EXPECT_EQ(POST("/unsubscribe/0123abcd", "kind=email").code, 404);
}

TEST_F(IntegrationTest, DuesReminderByEmail) {
    int64_t id = email_member(*member_repo, "dues@example.org");
    {
        auto u = db->prepare("UPDATE members SET is_paid=1, paid_until=date('now','+5 days') WHERE id=?");
        u.bind(1, id); u.step();
    }
    settings_repo->set("dues_reminder_days", "30");
    DuesRepository dues(*db);
    DuesService svc(dues, *settings_repo, *discord_client, *audit_svc);
    svc.set_notifier(std::make_shared<Notifier>(*db, *discord_client, mailer, "http://lug.test"));
    svc.run_once();
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].to, "dues@example.org");
    EXPECT_NE(out[0].subject.find("dues"), std::string::npos);
    EXPECT_NE(out[0].unsubscribe_url.find("?kind=dues_reminder"), std::string::npos);
    svc.run_once();                         // once per paid_until
    EXPECT_EQ(mailer->outbox().size(), 1u);
}
