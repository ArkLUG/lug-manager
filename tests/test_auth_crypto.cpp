// Password hashing and TOTP codes.
#include <gtest/gtest.h>
#include "auth/Password.hpp"
#include "auth/Totp.hpp"

TEST(Password, HashVerifyAndFormat) {
    std::string h = password::hash("correct horse battery");
    ASSERT_EQ(h.rfind("scrypt$15$8$1$", 0), 0u);
    EXPECT_TRUE(password::verify("correct horse battery", h));
    EXPECT_FALSE(password::verify("correct horse batterY", h));
    EXPECT_FALSE(password::verify("", h));
    EXPECT_NE(password::hash("correct horse battery"), h);        // salted
    EXPECT_FALSE(password::needs_rehash(h));
}

TEST(Password, RejectsMalformedOrHostileHashes) {
    EXPECT_FALSE(password::verify("x", ""));
    EXPECT_FALSE(password::verify("x", "plain"));
    EXPECT_FALSE(password::verify("x", "scrypt$15$8$1$zz$aa"));
    EXPECT_FALSE(password::verify("x", "scrypt$30$8$1$00$00"));       // absurd cost refused, not attempted
    EXPECT_FALSE(password::verify("x", "scrypt$15$8$1$00$"));
    EXPECT_TRUE(password::needs_rehash("scrypt$14$8$1$00$00"));
}

TEST(Password, Policy) {
    EXPECT_NE(password::policy_error("short"), "");
    EXPECT_NE(password::policy_error("password123"), "");
    EXPECT_NE(password::policy_error("aaaaaaaaaaaa"), "");
    EXPECT_NE(password::policy_error("ann.builder", "ann.builder@example.org"), "");
    EXPECT_NE(password::policy_error(std::string(201, 'x') + "y"), "");
    EXPECT_EQ(password::policy_error("bricks and more bricks"), "");
}

TEST(Totp, Rfc6238Vectors) {
    // RFC 6238 appendix B, SHA1 secret "12345678901234567890" (8 digits there; 6 is the low 6)
    std::string secret = totp::base32_encode({'1','2','3','4','5','6','7','8','9','0','1','2','3','4','5','6','7','8','9','0'});
    EXPECT_EQ(secret, "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
    EXPECT_EQ(totp::code_at_step(secret, 59 / 30, 8), 94287082);
    EXPECT_EQ(totp::code_at_step(secret, 1111111109 / 30, 8), 7081804);
    EXPECT_EQ(totp::code_at_step(secret, 1234567890 / 30, 8), 89005924);
    EXPECT_EQ(totp::code_at_step(secret, 2000000000 / 30, 8), 69279037);
    EXPECT_EQ(totp::code_at_step(secret, 59 / 30), 287082);
}

TEST(Totp, VerifyWindowAndReplay) {
    std::string s = totp::new_secret();
    ASSERT_EQ(s.size(), 32u);
    std::time_t now = 1700000000;
    auto code = [&](int64_t step) { char b[8]; std::snprintf(b, sizeof(b), "%06d", totp::code_at_step(s, step)); return std::string(b); };
    int64_t st = totp::step_at(now);
    EXPECT_EQ(totp::verify(s, code(st), now), st);
    EXPECT_EQ(totp::verify(s, code(st - 1), now), st - 1);              // a bit of clock drift
    EXPECT_EQ(totp::verify(s, code(st + 1), now), st + 1);
    EXPECT_EQ(totp::verify(s, code(st - 2), now), -1);                  // too old
    EXPECT_EQ(totp::verify(s, code(st), now, st), -1);                  // already used
    EXPECT_EQ(totp::verify(s, code(st).substr(0, 3) + " " + code(st).substr(3), now), st);   // "123 456"
    EXPECT_EQ(totp::verify(s, "12345", now), -1);
    EXPECT_EQ(totp::verify(s, "abcdef", now), -1);
    EXPECT_EQ(totp::verify("not base32!", code(st), now), -1);
}

TEST(Totp, ProvisioningUri) {
    EXPECT_EQ(totp::provisioning_uri("ABC", "ann@example.org", "Brickton LUG"),
              "otpauth://totp/Brickton%20LUG:ann%40example.org?secret=ABC&issuer=Brickton%20LUG&algorithm=SHA1&digits=6&period=30");
}
