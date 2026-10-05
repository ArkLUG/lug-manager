#include "utils/SecretBox.hpp"
#include "utils/Crypto.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sys/stat.h>
#include <vector>

namespace secretbox {
namespace {
std::mutex mu;
std::vector<unsigned char> key;   // 32 bytes when ready

std::string to_hex(const unsigned char* p, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { out += h[p[i] >> 4]; out += h[p[i] & 15]; }
    return out;
}
}

bool init(const std::string& data_dir) {
    std::lock_guard<std::mutex> l(mu);
    const std::string path = data_dir + "/secret.key";
    std::string hex;
    {
        std::ifstream in(path);
        if (in) std::getline(in, hex);
    }
    if (hex.size() != 64) {
        unsigned char k[32];
        if (RAND_bytes(k, sizeof(k)) != 1) return false;
        hex = to_hex(k, sizeof(k));
        std::error_code ec;
        std::filesystem::create_directories(data_dir, ec);
        {
            std::ofstream out(path, std::ios::trunc);
            if (!out) return false;
            out << hex << "\n";
        }
        ::chmod(path.c_str(), 0600);
    }
    std::vector<unsigned char> bytes;
    try { bytes = hex_decode(hex); } catch (...) { return false; }
    if (bytes.size() != 32) return false;
    key = std::move(bytes);
    return true;
}

bool ready() { std::lock_guard<std::mutex> l(mu); return key.size() == 32; }

std::string seal(const std::string& plain) {
    std::lock_guard<std::mutex> l(mu);
    if (key.size() != 32) return "";
    unsigned char nonce[12];
    if (RAND_bytes(nonce, sizeof(nonce)) != 1) return "";
    std::vector<unsigned char> buf(sizeof(nonce) + plain.size() + 16);
    std::copy(nonce, nonce + 12, buf.begin());
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int len = 0, total = 0;
    bool ok = c && EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, key.data(), nonce) == 1 &&
              EVP_EncryptUpdate(c, buf.data() + 12, &len, reinterpret_cast<const unsigned char*>(plain.data()),
                                static_cast<int>(plain.size())) == 1;
    total = len;
    ok = ok && EVP_EncryptFinal_ex(c, buf.data() + 12 + total, &len) == 1;
    total += len;
    ok = ok && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, buf.data() + 12 + total) == 1;
    if (c) EVP_CIPHER_CTX_free(c);
    if (!ok) return "";
    return "v1:" + to_hex(buf.data(), 12 + total + 16);
}

std::string open(const std::string& sealed) {
    std::lock_guard<std::mutex> l(mu);
    if (key.size() != 32 || sealed.rfind("v1:", 0) != 0) return "";
    std::vector<unsigned char> b;
    try { b = hex_decode(sealed.substr(3)); } catch (...) { return ""; }
    if (b.size() < 12 + 16) return "";
    const size_t n = b.size() - 12 - 16;
    std::string out(n, '\0');
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int len = 0;
    bool ok = c && EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, key.data(), b.data()) == 1 &&
              EVP_DecryptUpdate(c, reinterpret_cast<unsigned char*>(&out[0]), &len, b.data() + 12, static_cast<int>(n)) == 1 &&
              EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, b.data() + 12 + n) == 1 &&
              EVP_DecryptFinal_ex(c, reinterpret_cast<unsigned char*>(&out[0]) + len, &len) == 1;
    if (c) EVP_CIPHER_CTX_free(c);
    return ok ? out : "";
}

} // namespace secretbox
