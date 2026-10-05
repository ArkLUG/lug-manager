#pragma once
#include <string>

// Encrypts small secrets kept in the database (the SMTP password) with
// AES-256-GCM. The key is a random 32 bytes in <data dir>/secret.key (mode
// 600), created on first use, so a copy of lug.db alone doesn't reveal them.
// Moving to a new server without that file means re-entering the secrets.
namespace secretbox {

// Loads (or creates) the key file. Called at start-up and by tests.
bool init(const std::string& data_dir);
bool ready();
// "v1:" + hex(nonce | ciphertext | tag); "" if not ready or on error.
std::string seal(const std::string& plain);
// The plain text, or "" if it can't be opened (wrong key, tampered, empty).
std::string open(const std::string& sealed);

} // namespace secretbox
