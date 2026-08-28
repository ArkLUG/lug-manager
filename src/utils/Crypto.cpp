#include "utils/Crypto.hpp"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

std::string generate_random_hex(size_t num_bytes) {
    std::vector<unsigned char> buf(num_bytes);
    if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed: cannot generate random hex");
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : buf) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

std::string sha256_hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;

    if (EVP_Digest(input.data(), input.size(), digest, &digest_len, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("EVP_Digest failed: cannot compute sha256");
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        oss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return oss.str();
}

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<unsigned char> hex_decode(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw std::runtime_error("hex_decode: odd-length hex string");
    }
    std::vector<unsigned char> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hex_digit_value(hex[i]);
        int lo = hex_digit_value(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            throw std::runtime_error("hex_decode: non-hex character");
        }
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return out;
}
