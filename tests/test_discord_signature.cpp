#include <gtest/gtest.h>
#include "integrations/DiscordSignatureVerifier.hpp"
#include "utils/Crypto.hpp"
#include <openssl/evp.h>
#include <vector>
#include <string>

// ═══════════════════════════════════════════════════════════════════════════
// Discord Ed25519 interaction signature verification
//
// This is the actual security boundary for the /discord/interactions endpoint,
// so it gets the most explicit test coverage of anything in this feature.
// A fresh Ed25519 keypair is generated at test runtime via OpenSSL directly
// (no shelling out to the `openssl` CLI, no fixed hardcoded key material -
// generating fresh keys keeps this test independent of any real credentials).
// ═══════════════════════════════════════════════════════════════════════════

namespace {

struct TestKeypair {
    std::string public_key_hex;
    EVP_PKEY*   pkey = nullptr;

    ~TestKeypair() { if (pkey) EVP_PKEY_free(pkey); }
};

std::string to_hex(const unsigned char* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += digits[(data[i] >> 4) & 0xF];
        out += digits[data[i] & 0xF];
    }
    return out;
}

TestKeypair generate_test_keypair() {
    TestKeypair kp;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(pctx);
    EVP_PKEY* raw = nullptr;
    EVP_PKEY_keygen(pctx, &raw);
    EVP_PKEY_CTX_free(pctx);
    kp.pkey = raw;

    unsigned char pub[32];
    size_t pub_len = sizeof(pub);
    EVP_PKEY_get_raw_public_key(raw, pub, &pub_len);
    kp.public_key_hex = to_hex(pub, pub_len);
    return kp;
}

std::string sign_hex(EVP_PKEY* pkey, const std::string& message) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey);
    size_t sig_len = 0;
    EVP_DigestSign(ctx, nullptr, &sig_len,
                    reinterpret_cast<const unsigned char*>(message.data()), message.size());
    std::vector<unsigned char> sig(sig_len);
    EVP_DigestSign(ctx, sig.data(), &sig_len,
                    reinterpret_cast<const unsigned char*>(message.data()), message.size());
    EVP_MD_CTX_free(ctx);
    return to_hex(sig.data(), sig_len);
}

} // namespace

TEST(DiscordSignatureVerifier, ValidSignaturePasses) {
    auto kp = generate_test_keypair();
    std::string timestamp = "1700000000";
    std::string body = R"({"type":1})";
    std::string sig = sign_hex(kp.pkey, timestamp + body);

    EXPECT_TRUE(verify_discord_signature(kp.public_key_hex, sig, timestamp, body));
}

TEST(DiscordSignatureVerifier, TamperedBodyFails) {
    auto kp = generate_test_keypair();
    std::string timestamp = "1700000000";
    std::string body = R"({"type":1})";
    std::string sig = sign_hex(kp.pkey, timestamp + body);

    std::string tampered_body = R"({"type":2})";
    EXPECT_FALSE(verify_discord_signature(kp.public_key_hex, sig, timestamp, tampered_body));
}

TEST(DiscordSignatureVerifier, TamperedTimestampFails) {
    auto kp = generate_test_keypair();
    std::string timestamp = "1700000000";
    std::string body = R"({"type":1})";
    std::string sig = sign_hex(kp.pkey, timestamp + body);

    EXPECT_FALSE(verify_discord_signature(kp.public_key_hex, sig, "1700000001", body));
}

TEST(DiscordSignatureVerifier, WrongPublicKeyFails) {
    auto kp_a = generate_test_keypair();
    auto kp_b = generate_test_keypair();
    std::string timestamp = "1700000000";
    std::string body = R"({"type":1})";
    std::string sig = sign_hex(kp_a.pkey, timestamp + body);

    EXPECT_FALSE(verify_discord_signature(kp_b.public_key_hex, sig, timestamp, body));
}

TEST(DiscordSignatureVerifier, MalformedHexSignatureFailsWithoutThrowing) {
    auto kp = generate_test_keypair();
    EXPECT_NO_THROW({
        EXPECT_FALSE(verify_discord_signature(kp.public_key_hex, "not-hex-at-all!!", "1700000000", "{}"));
        EXPECT_FALSE(verify_discord_signature(kp.public_key_hex, "abc", "1700000000", "{}")); // odd length
        EXPECT_FALSE(verify_discord_signature(kp.public_key_hex, "ab", "1700000000", "{}"));   // too short
    });
}

TEST(DiscordSignatureVerifier, MalformedHexPublicKeyFailsWithoutThrowing) {
    std::string timestamp = "1700000000";
    std::string body = "{}";
    EXPECT_NO_THROW({
        EXPECT_FALSE(verify_discord_signature("not-hex!!", std::string(128, '0'), timestamp, body));
        EXPECT_FALSE(verify_discord_signature("abc", std::string(128, '0'), timestamp, body)); // odd length
        EXPECT_FALSE(verify_discord_signature("ab", std::string(128, '0'), timestamp, body));  // too short
    });
}

TEST(DiscordSignatureVerifier, EmptyInputsFail) {
    EXPECT_FALSE(verify_discord_signature("", "", "", ""));
}
