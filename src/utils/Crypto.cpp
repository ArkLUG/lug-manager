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
