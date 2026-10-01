#pragma once
// Time-based one-time passwords (RFC 6238: HMAC-SHA1, 30 s steps, 6 digits),
// as used by Google Authenticator, Authy, 1Password, Bitwarden, etc.
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace totp {

constexpr int kDigits = 6, kStep = 30;

inline std::string base32_encode(const std::vector<unsigned char>& data) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string out;
    int bits = 0;
    unsigned buf = 0;
    for (unsigned char c : data) {
        buf = (buf << 8) | c;
        bits += 8;
        while (bits >= 5) { out += A[(buf >> (bits - 5)) & 31]; bits -= 5; }
    }
    if (bits > 0) out += A[(buf << (5 - bits)) & 31];
    return out;
}

// Ignores spaces, dashes, padding and case. False on any other character.
inline bool base32_decode(const std::string& s, std::vector<unsigned char>& out) {
    out.clear();
    unsigned buf = 0;
    int bits = 0;
    for (char ch : s) {
        if (ch == ' ' || ch == '-' || ch == '=') continue;
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= '2' && c <= '7') v = c - '2' + 26;
        else return false;
        buf = (buf << 5) | static_cast<unsigned>(v);
        bits += 5;
        if (bits >= 8) { out.push_back(static_cast<unsigned char>((buf >> (bits - 8)) & 0xff)); bits -= 8; }
    }
    return !out.empty();
}

// A new random secret (160 bits), base32.
inline std::string new_secret() {
    std::vector<unsigned char> b(20);
    if (RAND_bytes(b.data(), static_cast<int>(b.size())) != 1) return "";
    return base32_encode(b);
}

inline int64_t step_at(std::time_t t) { return static_cast<int64_t>(t) / kStep; }

// The code for a step, or -1 if the secret is unusable.
inline int code_at_step(const std::string& secret_b32, int64_t step, int digits = kDigits) {
    std::vector<unsigned char> key;
    if (!base32_decode(secret_b32, key)) return -1;
    unsigned char msg[8];
    for (int i = 7; i >= 0; --i) { msg[i] = static_cast<unsigned char>(step & 0xff); step >>= 8; }
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (!HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()), msg, sizeof(msg), mac, &len) || len < 20) return -1;
    int off = mac[len - 1] & 0x0f;
    uint32_t bin = (static_cast<uint32_t>(mac[off] & 0x7f) << 24) | (static_cast<uint32_t>(mac[off + 1]) << 16) |
                   (static_cast<uint32_t>(mac[off + 2]) << 8) | mac[off + 3];
    uint32_t mod = 1;
    for (int i = 0; i < digits; ++i) mod *= 10;
    return static_cast<int>(bin % mod);
}

// Checks `code` against the steps around `now` (±1, for clock drift). Returns
// the matching step, or -1. The caller rejects steps <= the last one used, so
// a code can't be replayed.
inline int64_t verify(const std::string& secret_b32, const std::string& code_in, std::time_t now, int64_t last_used_step = 0) {
    std::string code;
    for (char c : code_in) if (std::isdigit(static_cast<unsigned char>(c))) code += c; else if (c != ' ' && c != '-') return -1;
    if (code.size() != static_cast<size_t>(kDigits)) return -1;
    int want = std::stoi(code);
    int64_t s = step_at(now);
    for (int64_t d : {0, -1, 1}) {
        int64_t st = s + d;
        if (st <= last_used_step) continue;
        if (code_at_step(secret_b32, st) == want) return st;
    }
    return -1;
}

inline std::string url_escape(const std::string& s) {
    static const char* x = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += x[c >> 4]; o += x[c & 15]; }
    }
    return o;
}

// otpauth://totp/<issuer>:<account>?secret=..&issuer=..  (what the QR code holds)
inline std::string provisioning_uri(const std::string& secret_b32, const std::string& account, const std::string& issuer) {
    return "otpauth://totp/" + url_escape(issuer) + ":" + url_escape(account) + "?secret=" + secret_b32 +
           "&issuer=" + url_escape(issuer) + "&algorithm=SHA1&digits=6&period=30";
}

} // namespace totp
