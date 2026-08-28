#include "utils/NameMatch.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace {

std::vector<std::string> split_tokens(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

bool is_name_suffix(const std::string& tok) {
    std::string t;
    for (char c : tok) if (std::isalpha(static_cast<unsigned char>(c))) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t == "jr" || t == "sr" || t == "ii" || t == "iii" || t == "iv";
}

} // namespace

std::string normalize_name(const std::string& name) {
    auto tokens = split_tokens(name);
    if (!tokens.empty() && is_name_suffix(tokens.back())) tokens.pop_back();

    std::ostringstream oss;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) oss << ' ';
        for (char c : tokens[i]) {
            oss << static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return oss.str();
}

int levenshtein_distance(const std::string& a, const std::string& b) {
    const size_t n = a.size(), m = b.size();
    if (n == 0) return static_cast<int>(m);
    if (m == 0) return static_cast<int>(n);

    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1));
    for (size_t i = 0; i <= n; ++i) dp[i][0] = static_cast<int>(i);
    for (size_t j = 0; j <= m; ++j) dp[0][j] = static_cast<int>(j);

    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,       // deletion
                dp[i][j - 1] + 1,       // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }
    return dp[n][m];
}

MatchConfidence classify_name_match(const std::string& a, const std::string& b) {
    std::string na = normalize_name(a);
    std::string nb = normalize_name(b);
    if (na.empty() || nb.empty()) return MatchConfidence::None;

    if (na == nb) return MatchConfidence::High;

    auto ta = split_tokens(na);
    auto tb = split_tokens(nb);
    if (ta.empty() || tb.empty()) return MatchConfidence::None;

    const std::string& first_a = ta.front();
    const std::string& first_b = tb.front();
    if (first_a != first_b) return MatchConfidence::None;

    bool has_last_a = ta.size() > 1;
    bool has_last_b = tb.size() > 1;

    if (has_last_a && has_last_b) {
        const std::string& last_a = ta.back();
        const std::string& last_b = tb.back();
        if (last_a == last_b) return MatchConfidence::High;
        if (levenshtein_distance(last_a, last_b) <= 2) return MatchConfidence::Medium;
        return MatchConfidence::None;
    }

    // One side has no last name at all — first-name-only match is a Medium
    // (not High) signal since it's the weakest evidence that still deserves
    // a review-queue entry rather than silent auto-import.
    return MatchConfidence::Medium;
}
