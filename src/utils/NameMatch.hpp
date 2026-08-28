#pragma once
#include <string>

// Small, deterministic name-similarity helper used to suggest (never auto-apply)
// a possible existing member match for an unmatched Discord guild member.

enum class MatchConfidence { None, Medium, High };

// Lowercases, collapses/strips whitespace, and strips a trailing Jr/Sr/II/III
// suffix (with or without punctuation) for comparison purposes.
std::string normalize_name(const std::string& name);

// Standard Levenshtein edit distance (case-sensitive; callers normalize first).
int levenshtein_distance(const std::string& a, const std::string& b);

// Classifies how plausibly two display names refer to the same person.
// - High:   normalized full strings are identical, or normalized first+last
//           tokens both match exactly.
// - Medium: first token matches exactly and the last-token edit distance is
//           <= 2 (typo tolerance), or one side has no last name and the
//           first token matches exactly.
// - None:   anything else.
// A non-None result is only ever a suggestion surfaced to an admin — nothing
// in this codebase auto-links based on it.
MatchConfidence classify_name_match(const std::string& a, const std::string& b);
