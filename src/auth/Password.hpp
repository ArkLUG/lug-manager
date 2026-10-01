#pragma once
// Password hashing with scrypt (OpenSSL EVP_PBE_scrypt).
// Stored as "scrypt$<logN>$<r>$<p>$<salt hex>$<hash hex>", so the cost can be
// raised later: needs_rehash() tells the sign-in code to re-hash on success.
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace password {

constexpr int kLogN = 15, kR = 8, kP = 1;           // 32 MiB, ~0.1 s
constexpr size_t kMinLength = 10, kMaxLength = 200;

inline std::string to_hex(const unsigned char* d, size_t n) {
    static const char* x = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s += x[d[i] >> 4]; s += x[d[i] & 15]; }
    return s;
}

inline bool from_hex(const std::string& h, std::vector<unsigned char>& out) {
    if (h.size() % 2) return false;
    out.clear();
    for (size_t i = 0; i < h.size(); i += 2) {
        unsigned v = 0;
        if (std::sscanf(h.c_str() + i, "%2x", &v) != 1) return false;
        out.push_back(static_cast<unsigned char>(v));
    }
    return true;
}

inline bool derive(const std::string& pw, const unsigned char* salt, size_t salt_len, int logN, int r, int p,
                   unsigned char* out, size_t out_len) {
    if (logN < 10 || logN > 20 || r < 1 || r > 32 || p < 1 || p > 16) return false;
    uint64_t N = 1ULL << logN;
    uint64_t maxmem = 128ULL * r * (N + p + 2) + (16ULL << 20);
    return EVP_PBE_scrypt(pw.data(), pw.size(), salt, salt_len, N, r, p, maxmem, out, out_len) == 1;
}

// "" if hashing failed (never store that).
inline std::string hash(const std::string& pw) {
    unsigned char salt[16], dk[32];
    if (RAND_bytes(salt, sizeof(salt)) != 1) return "";
    if (!derive(pw, salt, sizeof(salt), kLogN, kR, kP, dk, sizeof(dk))) return "";
    return "scrypt$" + std::to_string(kLogN) + "$" + std::to_string(kR) + "$" + std::to_string(kP) + "$" +
           to_hex(salt, sizeof(salt)) + "$" + to_hex(dk, sizeof(dk));
}

inline bool verify(const std::string& pw, const std::string& stored) {
    int logN = 0, r = 0, p = 0;
    char salt_hex[129] = {0}, dk_hex[129] = {0};
    if (std::sscanf(stored.c_str(), "scrypt$%d$%d$%d$%128[0-9a-f]$%128[0-9a-f]", &logN, &r, &p, salt_hex, dk_hex) != 5)
        return false;
    std::vector<unsigned char> salt, want;
    if (!from_hex(salt_hex, salt) || !from_hex(dk_hex, want) || want.empty() || want.size() > 64) return false;
    std::vector<unsigned char> got(want.size());
    if (!derive(pw, salt.data(), salt.size(), logN, r, p, got.data(), got.size())) return false;
    return CRYPTO_memcmp(got.data(), want.data(), want.size()) == 0;
}

// Spends the same time as a real check, for unknown accounts (no timing hint).
inline void verify_dummy(const std::string& pw) {
    static const std::string dummy = hash("not-a-real-password-placeholder");
    verify(pw, dummy);
}

inline bool needs_rehash(const std::string& stored) {
    return stored.rfind("scrypt$" + std::to_string(kLogN) + "$" + std::to_string(kR) + "$" + std::to_string(kP) + "$", 0) != 0;
}

// "" when acceptable, else what's wrong (plain English for the form).
inline std::string policy_error(const std::string& pw, const std::string& email = "") {
    if (pw.size() < kMinLength) return "Use at least " + std::to_string(kMinLength) + " characters.";
    if (pw.size() > kMaxLength) return "That's too long (" + std::to_string(kMaxLength) + " characters at most).";
    static const char* common[] = {"password12", "password123", "1234567890", "qwertyuiop", "letmein123",
                                   "iloveyou12", "legolego12", "lego123456", "0123456789", "abcdefghij"};
    std::string low;
    for (char c : pw) low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* c : common) if (low == c) return "That password is too easy to guess.";
    if (!email.empty()) {
        std::string e;
        for (char c : email) e += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low == e || low == e.substr(0, e.find('@'))) return "Don't use your email address as your password.";
    }
    bool all_same = true;
    for (char c : pw) if (c != pw[0]) { all_same = false; break; }
    if (all_same) return "That password is too easy to guess.";
    return "";
}

} // namespace password
