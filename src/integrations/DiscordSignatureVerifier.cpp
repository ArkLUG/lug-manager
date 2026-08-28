#include "integrations/DiscordSignatureVerifier.hpp"
#include "utils/Crypto.hpp"
#include <openssl/evp.h>
#include <vector>

bool verify_discord_signature(const std::string& public_key_hex,
                               const std::string& signature_hex,
                               const std::string& timestamp,
                               const std::string& raw_body) noexcept {
    try {
        std::vector<unsigned char> pubkey_bytes = hex_decode(public_key_hex);
        std::vector<unsigned char> sig_bytes    = hex_decode(signature_hex);

        // Ed25519 public keys are always 32 raw bytes, signatures always 64.
        if (pubkey_bytes.size() != 32 || sig_bytes.size() != 64) {
            return false;
        }

        // Discord's scheme: signature covers (timestamp + raw_body) concatenated
        // as bytes, in that exact order.
        std::string message = timestamp + raw_body;

        EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                       pubkey_bytes.data(), pubkey_bytes.size());
        if (!pkey) return false;

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            EVP_PKEY_free(pkey);
            return false;
        }

        bool ok = false;
        // No MD (digest) is passed for Ed25519 - it performs its own internal
        // hashing and does not support the streaming Update() calls RSA/ECDSA
        // do, hence the single-shot EVP_DigestVerify() below rather than
        // Init/Update/Final. EVP_DigestVerify's Ed25519 path is already
        // constant-time internally, so no separate manual timing-safe
        // comparison is needed here.
        if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1) {
            ok = (EVP_DigestVerify(ctx, sig_bytes.data(), sig_bytes.size(),
                                    reinterpret_cast<const unsigned char*>(message.data()),
                                    message.size()) == 1);
        }

        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return ok;
    } catch (...) {
        // Malformed hex (odd length, non-hex chars) or any other failure -
        // fail closed rather than let an exception escape to the public
        // route handler.
        return false;
    }
}
