// Email + password sign-in, TOTP two-factor, recovery codes, required 2FA,
// forgotten passwords and admin set-password links.
#include "integration_test_base.hpp"
#include "auth/Totp.hpp"
#include "fake_discord.hpp"

namespace {

std::string after(const std::string& s, const std::string& marker) {
    auto p = s.find(marker);
    if (p == std::string::npos) return "";
    p += marker.size();
    auto e = s.find_first_of(" \r\n\"?;<", p);
    return s.substr(p, e == std::string::npos ? std::string::npos : e - p);
}
std::string enc(const std::string& v) {
    std::string o;
    char b[4];
    for (unsigned char c : v) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') o += static_cast<char>(c);
        else { std::snprintf(b, sizeof(b), "%%%02X", c); o += b; }
    }
    return o;
}
std::string text(SqliteDatabase& db, const std::string& sql) {
    auto st = db.prepare(sql);
    return st.step() ? st.col_text(0) : "";
}
int64_t num(SqliteDatabase& db, const std::string& sql) {
    auto st = db.prepare(sql);
    return st.step() ? st.col_int(0) : -1;
}
std::string code_now(const std::string& secret, int offset = 0) {
    char b[8];
    std::snprintf(b, sizeof(b), "%06d", totp::code_at_step(secret, totp::step_at(std::time(nullptr)) + offset));
    return b;
}

} // namespace

class AccountsTest : public IntegrationTest {
protected:
    bool signed_out(const std::string& session) {
        auto r = GET("/account", session);
        return (r.code == 303 || r.code == 307) && r.location.find("/login") != std::string::npos;
    }
    std::shared_ptr<Mailer> email_on() {
        // The fixture's mailer captures; email features look at Mailer::enabled().
        return mailer;
    }
    int64_t member_with_email(const std::string& email, const std::string& role = "member", const std::string& discord = "") {
        Member m;
        m.first_name = "Pat"; m.last_name = "Word"; m.display_name = "Pat W.";
        m.email = email; m.role = role; m.discord_user_id = discord;
        return member_repo->create(m).id;
    }
    std::string sign_in(const std::string& email, const std::string& pw, Response* out = nullptr) {
        auto r = POST("/auth/password", "email=" + enc(email) + "&password=" + enc(pw));
        if (out) *out = r;
        std::string s = after(r.headers, "session=");
        return s;
    }
    // Sets a password the normal way, as the signed-in member.
    void set_pw(int64_t id, const std::string& pw) {
        std::string t = session_store->create(id, "member", "Pat W.");
        ASSERT_EQ(POST("/account/password", "password=" + enc(pw) + "&confirm=" + enc(pw), t).code, 200);
    }
    std::string enable_2fa(int64_t id, const std::string& session) {
        EXPECT_EQ(POST("/account/2fa/setup", "", session).code, 200);
        std::string secret = text(*db, "SELECT totp_pending_secret FROM members WHERE id=" + std::to_string(id));
        auto r = POST("/account/2fa/enable", "code=" + code_now(secret), session);
        EXPECT_EQ(r.code, 200);
        return secret;
    }
};

TEST_F(AccountsTest, PasswordSetAndSignIn) {
    int64_t id = member_with_email("Pat@Example.org");
    std::string s = session_store->create(id, "member", "Pat W.");
    auto pg = GET("/account/security", s);
    EXPECT_EQ(pg.code, 200);
    expect_contains(pg, "Set a password to sign in with");

    // Policy and confirmation
    expect_contains(POST("/account/password", "password=short&confirm=short", s), "at least 10 characters");
    expect_contains(POST("/account/password", "password=password123&confirm=password123", s), "too easy to guess");
    expect_contains(POST("/account/password", "password=bricks+on+bricks&confirm=bricks+on+brick", s), "two new passwords don");
    std::string other = session_store->create(id, "member", "Pat W.");
    auto ok = POST("/account/password", "password=bricks+on+bricks&confirm=bricks+on+bricks", s);
    EXPECT_EQ(ok.code, 200);
    expect_contains(ok, "Password set");
    EXPECT_EQ(text(*db, "SELECT substr(password_hash,1,7) FROM members WHERE id=" + std::to_string(id)), "scrypt$");
    EXPECT_TRUE(signed_out(other));                  // other devices signed out
    EXPECT_EQ(GET("/account", s).code, 200);                      // this one kept

    // Sign in (email is case-insensitive)
    expect_contains(GET("/login"), "action=\"/auth/password\"");
    Response r;
    std::string session = sign_in("pat@EXAMPLE.org", "bricks on bricks", &r);
    EXPECT_EQ(r.code, 303);
    EXPECT_NE(r.location.find("/dashboard"), std::string::npos);
    ASSERT_EQ(session.size(), 64u);
    EXPECT_EQ(GET("/account", session).code, 200);
    EXPECT_EQ(num(*db, "SELECT COUNT(*) FROM audit_log WHERE action='auth.login' AND details='Signed in (password)'"), 1);

    // Wrong password / unknown email: same answer
    sign_in("pat@example.org", "wrong password!", &r);
    EXPECT_NE(r.location.find("/login?error=password"), std::string::npos);
    sign_in("nobody@example.org", "bricks on bricks", &r);
    EXPECT_NE(r.location.find("/login?error=password"), std::string::npos);
    expect_contains(GET("/login?error=password"), "The email or password is wrong");
    EXPECT_EQ(num(*db, "SELECT COUNT(*) FROM audit_log WHERE action='auth.login_failed'"), 1);

    // Changing it needs the current one, and emails a heads-up
    expect_contains(POST("/account/password", "current=nope&password=new+brick+wall&confirm=new+brick+wall", session), "current password isn");
    EXPECT_EQ(POST("/account/password", "current=bricks+on+bricks&password=new+brick+wall&confirm=new+brick+wall", session).code, 200);
    auto out = mailer->outbox();
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.back().subject, "Your LUG Manager password was changed");
    sign_in("pat@example.org", "bricks on bricks", &r);
    EXPECT_NE(r.location.find("error=password"), std::string::npos);
    EXPECT_EQ(sign_in("pat@example.org", "new brick wall").size(), 64u);

    // An email on two member records can't sign in or get a password
    member_with_email("PAT@example.org");
    sign_in("pat@example.org", "new brick wall", &r);
    EXPECT_NE(r.location.find("error=password"), std::string::npos);
    expect_contains(POST("/account/password", "current=new+brick+wall&password=x+y+z+w+v+u&confirm=x+y+z+w+v+u", session),
                    "Another member has the same email");

    // Turned off by an admin: the form goes away and sign-in refuses
    settings_repo->set("auth_password_enabled", "0");
    expect_not_contains(GET("/login"), "action=\"/auth/password\"");
    sign_in("pat@example.org", "new brick wall", &r);
    EXPECT_EQ(r.location.find("/dashboard"), std::string::npos);
}

TEST_F(AccountsTest, SignInLimits) {
    int64_t id = member_with_email("lim@example.org");
    set_pw(id, "limit test password");
    Response r;
    for (int i = 0; i < 8; ++i) sign_in("lim@example.org", "wrong one here", &r);
    sign_in("lim@example.org", "limit test password", &r);              // right, but locked for now
    EXPECT_NE(r.location.find("/login?error=locked"), std::string::npos);
    // Unknown addresses are limited the same way (no hint which exist)
    for (int i = 0; i < 8; ++i) sign_in("ghost@example.org", "whatever pw", &r);
    sign_in("ghost@example.org", "whatever pw", &r);
    EXPECT_NE(r.location.find("error=locked"), std::string::npos);
}

TEST_F(AccountsTest, TwoFactorSetupSignInAndRecovery) {
    int64_t id = member_with_email("two@example.org");
    set_pw(id, "two factor please");
    std::string s = sign_in("two@example.org", "two factor please");

    // Setup: QR + key, wrong code refused, right code turns it on and shows codes once
    auto setup = POST("/account/2fa/setup", "", s);
    expect_contains(setup, "data-qr=\"otpauth:");
    std::string secret = text(*db, "SELECT totp_pending_secret FROM members WHERE id=" + std::to_string(id));
    ASSERT_EQ(secret.size(), 32u);
    EXPECT_EQ(POST("/account/2fa/enable", "code=000000", s).code, 400);
    std::string other = session_store->create(id, "member", "Pat W.");
    auto on = POST("/account/2fa/enable", "code=" + code_now(secret), s);
    EXPECT_EQ(on.code, 200);
    expect_contains(on, "Your recovery codes");
    std::string first_code = after(on.body, "<li class=\"bg-gray-50 border border-gray-200 rounded px-3 py-1.5\">");
    ASSERT_EQ(first_code.size(), 11u);                                  // xxxxx-xxxxx
    EXPECT_EQ(num(*db, "SELECT COUNT(*) FROM recovery_codes WHERE member_id=" + std::to_string(id)), 10);
    EXPECT_TRUE(signed_out(other));                        // other devices signed out
    expect_not_contains(GET("/account/security", s), "Your recovery codes");

    // Password alone now leads to the code page, not a session
    Response r;
    sign_in("two@example.org", "two factor please", &r);
    EXPECT_NE(r.location.find("/auth/2fa"), std::string::npos);
    EXPECT_EQ(after(r.headers, "session="), "");
    std::string mfa = after(r.headers, "mfa=");
    ASSERT_EQ(mfa.size(), 64u);
    std::vector<std::string> cookie{"Cookie: mfa=" + mfa};
    EXPECT_NE(GET("/auth/2fa").location.find("/login?error=expired"), std::string::npos);   // no cookie
    EXPECT_EQ(http("GET", "/auth/2fa", "", "", false, "", false, cookie).code, 200);
    auto bad = http("POST", "/auth/2fa", "code=123456", "", false, "", false, cookie);
    EXPECT_EQ(bad.code, 400);
    expect_contains(bad, "Tries left: 4");
    // The code used to turn 2FA on can't be used again; the next one can
    std::string next = code_now(secret, 1);
    auto good = http("POST", "/auth/2fa", "code=" + next, "", false, "", false, cookie);
    EXPECT_EQ(good.code, 303);
    EXPECT_NE(good.location.find("/dashboard"), std::string::npos);
    EXPECT_EQ(after(good.headers, "session=").size(), 64u);
    EXPECT_NE(http("POST", "/auth/2fa", "code=" + next, "", false, "", false, cookie).location.find("error=expired"), std::string::npos);
    sign_in("two@example.org", "two factor please", &r);
    std::vector<std::string> c2{"Cookie: mfa=" + after(r.headers, "mfa=")};
    EXPECT_EQ(http("POST", "/auth/2fa", "code=" + next, "", false, "", false, c2).code, 400);   // replay refused

    // A recovery code works once (any case, with or without the dash)
    std::string rc = first_code;
    std::transform(rc.begin(), rc.end(), rc.begin(), ::toupper);
    auto rec = http("POST", "/auth/2fa", "code=" + enc(rc), "", false, "", false, c2);
    EXPECT_EQ(rec.code, 303);
    EXPECT_NE(rec.location.find("/account/security?recovery_used=1"), std::string::npos);
    expect_contains(GET("/account/security?recovery_used=1", after(rec.headers, "session=")), "Recovery codes left: <strong>9</strong>");
    sign_in("two@example.org", "two factor please", &r);
    std::vector<std::string> c3{"Cookie: mfa=" + after(r.headers, "mfa=")};
    EXPECT_EQ(http("POST", "/auth/2fa", "code=" + enc(first_code), "", false, "", false, c3).code, 400);

    // Five wrong codes end the attempt
    for (int i = 0; i < 4; ++i) http("POST", "/auth/2fa", "code=000000", "", false, "", false, c3);
    EXPECT_NE(http("POST", "/auth/2fa", "code=" + code_now(secret, 1), "", false, "", false, c3).location.find("error=expired"),
              std::string::npos);

    // New recovery codes and turning it off both need a current code
    std::string fresh = session_store->create(id, "member", "Pat W.");
    EXPECT_EQ(POST("/account/2fa/recovery", "code=000000", fresh).code, 400);
    EXPECT_EQ(POST("/account/2fa/disable", "code=000000", fresh).code, 400);
    // (codes for steps already used are refused, so wait for a fresh one by using the recovery path)
    std::string rc2 = after(on.body, first_code + "</li><li class=\"bg-gray-50 border border-gray-200 rounded px-3 py-1.5\">");
    ASSERT_EQ(rc2.size(), 11u);
    EXPECT_EQ(POST("/account/2fa/disable", "code=" + rc2, fresh).code, 200);
    EXPECT_EQ(num(*db, "SELECT totp_enabled_at IS NULL FROM members WHERE id=" + std::to_string(id)), 1);
    EXPECT_EQ(num(*db, "SELECT COUNT(*) FROM recovery_codes WHERE member_id=" + std::to_string(id)), 0);
    EXPECT_EQ(sign_in("two@example.org", "two factor please").size(), 64u);
    for (const char* a : {"auth.2fa_on", "auth.2fa_off", "auth.2fa_failed"})
        EXPECT_GE(num(*db, std::string("SELECT COUNT(*) FROM audit_log WHERE action='") + a + "'"), 1) << a;
}

TEST_F(AccountsTest, TwoFactorAlsoAppliesToEmailLinksAndDiscord) {
    int64_t id = member_with_email("link@example.org", "member", "300000000000000001");
    std::string s = session_store->create(id, "member", "Pat W.");
    std::string secret = enable_2fa(id, s);

    // Email link: confirm leads to the code page
    POST("/auth/email", "email=link%40example.org");
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 1u);
    std::string token = after(out[0].body, "/auth/email/");
    auto in = POST("/auth/email/" + token, "");
    EXPECT_NE(in.location.find("/auth/2fa"), std::string::npos);
    EXPECT_EQ(after(in.headers, "session="), "");

    // Discord sign-in (fake Discord): also the code page
    FakeDiscord fake;
    fake.oauth_user_id = "300000000000000001";
    fake.add_member("300000000000000001", "pat");
    discord_client->reconfigure(fake.guild_id, "", "", "", "", "America/Chicago");
    std::vector<std::string> st{"Cookie: oauth_state=nonce123"};
    auto cb = http("GET", "/auth/callback?code=abc&state=nonce123", "", "", false, "", false, st);
    EXPECT_NE(cb.location.find("/auth/2fa"), std::string::npos) << cb.location;
    EXPECT_EQ(after(cb.headers, "session="), "");
    std::vector<std::string> c{"Cookie: mfa=" + after(cb.headers, "mfa=")};
    auto done = http("POST", "/auth/2fa", "code=" + code_now(secret, 1), "", false, "", false, c);
    EXPECT_EQ(done.code, 303);
    EXPECT_EQ(after(done.headers, "session=").size(), 64u);
    EXPECT_EQ(num(*db, "SELECT COUNT(*) FROM audit_log WHERE action='auth.login' AND details='Signed in (discord, 2FA code)'"), 1);

    // Discord sign-in can be switched off
    settings_repo->set("auth_discord_enabled", "0");
    expect_not_contains(GET("/login"), "Sign in with Discord");
    EXPECT_NE(GET("/auth/login").location.find("/login"), std::string::npos);
}

TEST_F(AccountsTest, RequiredTwoFactor) {
    int64_t id = member_with_email("req@example.org");
    std::string member = session_store->create(id, "member", "Pat W.");
    settings_repo->set("auth_require_2fa", "staff");
    auto d = GET("/dashboard", admin_token);
    EXPECT_EQ(d.code, 303);
    EXPECT_NE(d.location.find("/account/security"), std::string::npos);
    auto hx = GET_HTMX("/members", admin_token);
    EXPECT_NE(hx.headers.find("HX-Redirect: /account/security"), std::string::npos);
    auto sec = GET("/account/security", admin_token);
    EXPECT_EQ(sec.code, 200);
    expect_contains(sec, "Two-factor is required for your account");
    EXPECT_EQ(GET("/dashboard", member).code, 200);                      // members not affected by "staff"
    EXPECT_EQ(POST("/auth/logout", "", admin_token).code, 303);          // can still sign out

    settings_repo->set("auth_require_2fa", "everyone");
    EXPECT_EQ(GET("/dashboard", member).code, 303);
    std::string secret = enable_2fa(id, member);
    EXPECT_EQ(GET("/dashboard", member).code, 200);
    // Can't turn it off while required
    expect_contains(POST("/account/2fa/disable", "code=" + code_now(secret, 1), member), "required for your account");
    EXPECT_EQ(num(*db, "SELECT totp_enabled_at IS NOT NULL FROM members WHERE id=" + std::to_string(id)), 1);
}

TEST_F(AccountsTest, ForgotPasswordAndAdminLinks) {
    int64_t id = member_with_email("forgot@example.org");
    expect_contains(GET("/login"), "Forgot it?");
    EXPECT_EQ(POST("/auth/forgot", "email=nobody%40example.org").location.find("/login?forgot_sent=1") != std::string::npos, true);
    EXPECT_TRUE(mailer->outbox().empty());
    POST("/auth/forgot", "email=forgot%40example.org");
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].subject, "Set a new LUG Manager password");
    std::string token = after(out[0].body, "/auth/reset/");
    ASSERT_EQ(token.size(), 64u);

    auto form = GET("/auth/reset/" + token);
    EXPECT_EQ(form.code, 200);
    expect_contains(form, "forgot@example.org");
    EXPECT_EQ(POST("/auth/reset/" + token, "password=reset+me+please&confirm=reset+me+pleas").code, 400);
    EXPECT_EQ(POST("/auth/reset/" + token, "password=short&confirm=short").code, 400);
    std::string s = session_store->create(id, "member", "Pat W.");
    auto done = POST("/auth/reset/" + token, "password=reset+me+please&confirm=reset+me+please");
    EXPECT_NE(done.location.find("/login?reset=1"), std::string::npos);
    EXPECT_TRUE(signed_out(s));                             // signed out everywhere
    EXPECT_NE(POST("/auth/reset/" + token, "password=again+and+again&confirm=again+and+again").location.find("error=link"), std::string::npos);
    EXPECT_EQ(sign_in("forgot@example.org", "reset me please").size(), 64u);
    for (int i = 0; i < 4; ++i) POST("/auth/forgot", "email=forgot%40example.org");
    EXPECT_EQ(mailer->outbox().size(), 3u);                               // 3 an hour

    // Admin-made links: admins only, 24 hours, optionally emailed
    std::string url = "/members/" + std::to_string(id) + "/password-link";
    EXPECT_EQ(POST(url, "", member_token).code, 403);
    auto link = POST(url, "", admin_token);
    EXPECT_EQ(link.code, 200);
    std::string body = link.body;
    for (size_t p; (p = body.find("&#x2F;")) != std::string::npos;) body.replace(p, 6, "/");
    std::string t2 = after(body, "/auth/reset/");
    ASSERT_EQ(t2.size(), 64u);
    EXPECT_EQ(GET("/auth/reset/" + t2).code, 200);
    mailer->clear_outbox();
    POST(url + "?email=1", "", admin_token);
    ASSERT_EQ(mailer->outbox().size(), 1u);
    EXPECT_EQ(mailer->outbox()[0].subject, "Set your LUG Manager password");
    // Setting a password voids the other outstanding links
    std::string t3 = after(mailer->outbox()[0].body, "/auth/reset/");
    POST("/auth/reset/" + t2, "password=admin+made+link&confirm=admin+made+link");
    EXPECT_NE(GET("/auth/reset/" + t3).location.find("error=link"), std::string::npos);
    // No usable email: no link
    Member nomail; nomail.first_name = "No"; nomail.display_name = "No M."; nomail.role = "member";
    int64_t nm = member_repo->create(nomail).id;
    EXPECT_EQ(POST("/members/" + std::to_string(nm) + "/password-link", "", admin_token).code, 400);
}

TEST_F(AccountsTest, AdminResetTwoFactorAndSignInSettings) {
    int64_t id = member_with_email("lost@example.org");
    std::string s = session_store->create(id, "member", "Pat W.");
    enable_2fa(id, s);
    std::string url = "/members/" + std::to_string(id) + "/reset-2fa";
    EXPECT_EQ(POST(url, "", member_token).code, 403);
    expect_contains(POST(url, "", admin_token), "Two-factor turned off");
    EXPECT_EQ(num(*db, "SELECT totp_enabled_at IS NULL FROM members WHERE id=" + std::to_string(id)), 1);
    EXPECT_TRUE(signed_out(s));
    expect_contains(POST(url, "", admin_token), "on for them");

    EXPECT_EQ(GET("/settings/sign-in", member_token).code, 403);
    auto pg = GET("/settings/sign-in", admin_token);
    expect_contains(pg, "Require two-factor");
    POST("/settings/sign-in", "require_2fa=bogus", admin_token);
    EXPECT_EQ(settings_repo->get("auth_require_2fa", ""), "off");
    auto saved = POST("/settings/sign-in", "password=1&require_2fa=everyone", admin_token);
    expect_contains(saved, "Saved.");
    EXPECT_EQ(settings_repo->get("auth_require_2fa", ""), "everyone");
    EXPECT_EQ(settings_repo->get("auth_email_links", ""), "0");
    EXPECT_EQ(settings_repo->get("auth_password_enabled", ""), "1");
    // The admin has no two-factor yet, so from now on they're sent to set it up first
    EXPECT_NE(POST("/settings/sign-in", "require_2fa=off", admin_token).location.find("/account/security"), std::string::npos);
    EXPECT_EQ(settings_repo->get("auth_require_2fa", ""), "everyone");
}

TEST_F(AccountsTest, EmailChangeVoidsOutstandingLinks) {
    int64_t id = member_with_email("old@example.org");
    POST("/auth/email", "email=old%40example.org");
    POST("/auth/forgot", "email=old%40example.org");
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 2u);
    std::string link = after(out[0].body, "/auth/email/"), reset = after(out[1].body, "/auth/reset/");
    EXPECT_EQ(GET("/auth/email/" + link).code, 200);
    EXPECT_EQ(GET("/auth/reset/" + reset).code, 200);
    Member upd = *member_repo->find_by_id(id);
    upd.email = "new@example.org";
    member_svc->update(id, upd);
    EXPECT_NE(GET("/auth/email/" + link).location.find("error=link"), std::string::npos);
    EXPECT_NE(GET("/auth/reset/" + reset).location.find("error=link"), std::string::npos);
}

// Emailed links always point at LUG_PUBLIC_URL, never at a Host header the
// requester chose (that would hand them the victim's token).
TEST_F(AccountsTest, EmailedLinksIgnoreRequestHost) {
    member_with_email("host@example.org");
    http("POST", "/auth/forgot", "email=host%40example.org", "", false, "", false,
         {"Host: evil.example", "X-Forwarded-Host: evil.example"});
    http("POST", "/auth/email", "email=host%40example.org", "", false, "", false,
         {"Host: evil.example", "X-Forwarded-Host: evil.example"});
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 2u);
    for (const auto& m : out) {
        EXPECT_EQ(m.body.find("evil.example"), std::string::npos) << m.body;
        EXPECT_NE(m.body.find("http://lug.test/auth/"), std::string::npos) << m.body;
    }
}

// An email a member types in themselves must be confirmed before it signs in
// or gets mail; admins' edits and Discord-verified emails count as confirmed.
TEST_F(AccountsTest, SelfEnteredEmailNeedsConfirming) {
    mailer->clear_outbox();
    EXPECT_EQ(POST("/members/me", "first_name=Reg&last_name=User&email=Reg.New%40example.org", member_token).code, 200);
    EXPECT_EQ(num(*db, "SELECT email_confirmed FROM members WHERE id=" + std::to_string(regular_member_id)), 0);
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].subject, "Confirm your email for LUG Manager");
    std::string token = after(out[0].body, "/account/confirm-email/");
    ASSERT_EQ(token.size(), 64u);
    expect_contains(GET("/account", member_token), "Please confirm your email");

    // Not usable yet: no sign-in link goes out for it
    mailer->clear_outbox();
    POST("/auth/email", "email=reg.new%40example.org");
    EXPECT_TRUE(mailer->outbox().empty());

    // Opening the link shows a button (link scanners can't confirm); posting confirms
    auto page = GET("/account/confirm-email/" + token);
    EXPECT_EQ(page.code, 200);
    expect_contains(page, "Confirm my email");
    EXPECT_EQ(num(*db, "SELECT email_confirmed FROM members WHERE id=" + std::to_string(regular_member_id)), 0);
    auto done = POST("/account/confirm-email/" + token, "");
    EXPECT_EQ(done.code, 200);
    expect_contains(done, "Email confirmed");
    EXPECT_EQ(num(*db, "SELECT email_confirmed FROM members WHERE id=" + std::to_string(regular_member_id)), 1);
    EXPECT_EQ(POST("/account/confirm-email/" + token, "").code, 400);   // once only
    POST("/auth/email", "email=reg.new%40example.org");
    EXPECT_EQ(mailer->outbox().size(), 1u);                              // now it signs in

    // An admin setting an email: confirmed straight away
    POST("/members/" + std::to_string(regular_member_id), "first_name=Reg&last_name=User&email=admin.set%40example.org", admin_token);
    EXPECT_EQ(num(*db, "SELECT email_confirmed FROM members WHERE id=" + std::to_string(regular_member_id)), 1);
}
