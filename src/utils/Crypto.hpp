#pragma once
#include <string>
#include <cstddef>

// Small shared crypto helpers built on OpenSSL (already linked project-wide).
// generate_random_hex mirrors the RAND_bytes-to-hex idiom used by
// SessionStore::generate_token() and the checkin_token generators in
// EventService/MeetingService, parameterized on byte count for reuse.

// Generates num_bytes of cryptographically random data, hex-encoded
// (result is 2*num_bytes lowercase hex chars).
std::string generate_random_hex(size_t num_bytes = 32);

// Returns the lowercase hex-encoded SHA-256 digest of input.
std::string sha256_hex(const std::string& input);
