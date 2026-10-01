#pragma once
#include <string>

// Verifies an inbound Discord Interactions webhook request per Discord's documented
// Ed25519 scheme: the signature covers (timestamp + raw_body) concatenated as bytes,
// checked against the Application's public key (from the Discord Developer Portal's
// "Public Key" field - distinct from the bot token and OAuth2 client secret).
//
// Fails closed (returns false) on any malformed input, wrong-length hex, or OpenSSL
// failure - never throws, safe to call directly from the public route handler with
// no surrounding try/catch required.
bool verify_discord_signature(const std::string& public_key_hex,
                               const std::string& signature_hex,
                               const std::string& timestamp,
                               const std::string& raw_body) noexcept;
