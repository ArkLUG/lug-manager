#pragma once
// The site's public web address and its outgoing email server.
//
// Both are set by an admin in Settings > Email & address and stored in the
// database. An environment variable set for one (LUG_PUBLIC_URL,
// LUG_SMTP_URL / _USER / _FROM) wins and shows as locked on that page. The
// SMTP password is a secret, so it's environment-only (LUG_SMTP_PASSWORD).
//
// Callers ask at the moment they need a value, so a change in Settings takes
// effect straight away.
#include "integrations/email/Mailer.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include <mutex>
#include <string>

namespace site {

namespace detail {
struct State {
    SettingsRepository* settings = nullptr;
    std::string env_url;
    Mailer::Config env_smtp;
    std::mutex mu;
};
inline State& state() { static State s; return s; }
inline std::string setting(const char* key) {
    auto& s = state();
    std::lock_guard<std::mutex> l(s.mu);
    return s.settings ? s.settings->get(key, "") : "";
}
}

// "https://lug.example.org" (no trailing slash) if it's a plain http(s) URL
// without spaces, quotes or angle brackets; "" otherwise.
inline std::string normalize_url(std::string u) {
    while (!u.empty() && (u.back() == '/' || u.back() == ' ')) u.pop_back();
    while (!u.empty() && u.front() == ' ') u.erase(0, 1);
    if (u.rfind("https://", 0) != 0 && u.rfind("http://", 0) != 0) return "";
    if (u.find_first_of(" \t\r\n\"'<>\\") != std::string::npos || u.size() > 200) return "";
    if (u.find('/', u.find("://") + 3) != std::string::npos) return "";   // just the site, no path
    return u.size() > u.find("://") + 3 ? u : "";
}

// Called once at start-up (and by tests) with the environment's values.
inline void bind(SettingsRepository* settings, const std::string& env_public_url, Mailer::Config env_smtp) {
    auto& s = detail::state();
    std::lock_guard<std::mutex> l(s.mu);
    s.settings = settings;
    s.env_url = normalize_url(env_public_url);
    s.env_smtp = std::move(env_smtp);
}

inline bool public_url_locked() { auto& s = detail::state(); std::lock_guard<std::mutex> l(s.mu); return !s.env_url.empty(); }

inline std::string public_url() {
    {
        auto& s = detail::state();
        std::lock_guard<std::mutex> l(s.mu);
        if (!s.env_url.empty()) return s.env_url;
    }
    return normalize_url(detail::setting("public_url"));
}

// Which SMTP fields the environment sets (and so can't be changed in the app).
struct SmtpLocks { bool url = false, user = false, from = false, password = false; };
inline SmtpLocks smtp_locks() {
    auto& s = detail::state();
    std::lock_guard<std::mutex> l(s.mu);
    return {!s.env_smtp.url.empty(), !s.env_smtp.user.empty(), !s.env_smtp.from.empty(), !s.env_smtp.password.empty()};
}

inline Mailer::Config smtp() {
    Mailer::Config env;
    { auto& s = detail::state(); std::lock_guard<std::mutex> l(s.mu); env = s.env_smtp; }
    Mailer::Config c;
    c.url      = !env.url.empty()  ? env.url  : detail::setting("smtp_url");
    c.user     = !env.user.empty() ? env.user : detail::setting("smtp_user");
    c.from     = !env.from.empty() ? env.from : detail::setting("smtp_from");
    c.password = env.password;
    return c;
}

} // namespace site
