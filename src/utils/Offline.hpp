#pragma once
#include <curl/curl.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// LUG_OFFLINE=1 hard-disables every outbound call (Discord, Discord OAuth,
// Google Calendar, SMTP): they fail as if the network were down. For tests,
// rehearsals against copies of real data, and local development - nothing
// can be posted, DMed, emailed or synced to a real server by accident.
// Requests to this machine (127.0.0.1 / localhost - the test fakes) still work.
inline bool offline_mode() {
    const char* v = std::getenv("LUG_OFFLINE");
    return v && std::strcmp(v, "1") == 0;
}

inline bool is_loopback_url(const std::string& url) {
    for (const char* p : {"http://127.0.0.1:", "http://127.0.0.1/", "http://localhost:", "http://localhost/"})
        if (url.rfind(p, 0) == 0) return true;
    return false;
}

// Base URLs, overridable only to a loopback address (the fake servers used
// by the tests) so a bot token can never be sent to some other host.
//   LUG_DISCORD_BASE  replaces https://discord.com
//   LUG_GOOGLE_BASE   replaces https://www.googleapis.com
inline std::string service_base(const char* env, const char* real) {
    const char* v = std::getenv(env);
    if (v && is_loopback_url(v)) {
        std::string s = v;
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    }
    return real;
}
inline std::string discord_base() { return service_base("LUG_DISCORD_BASE", "https://discord.com"); }
inline std::string google_base()  { return service_base("LUG_GOOGLE_BASE", "https://www.googleapis.com"); }

// Use instead of curl_easy_perform for anything that may leave this machine.
inline CURLcode guarded_perform(CURL* curl) {
    if (offline_mode()) {
        char* url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
        if (!url || !is_loopback_url(url)) {
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true))
                std::cerr << "[offline] LUG_OFFLINE=1: blocked outbound request (Discord/Google/SMTP are disabled)\n";
            return CURLE_COULDNT_CONNECT;
        }
    }
    return curl_easy_perform(curl);
}
