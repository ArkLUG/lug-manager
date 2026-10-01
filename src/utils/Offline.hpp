#pragma once
#include <curl/curl.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>

// LUG_OFFLINE=1 hard-disables every outbound call (Discord, Discord OAuth,
// Google Calendar, SMTP): they fail as if the network were down. For tests,
// rehearsals against copies of real data, and local development - nothing
// can be posted, DMed, emailed or synced to a real server by accident.
inline bool offline_mode() {
    const char* v = std::getenv("LUG_OFFLINE");
    return v && std::strcmp(v, "1") == 0;
}

// Use instead of curl_easy_perform for anything leaving this machine.
inline CURLcode guarded_perform(CURL* curl) {
    if (offline_mode()) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true))
            std::cerr << "[offline] LUG_OFFLINE=1: blocked outbound request (Discord/Google/SMTP are disabled)\n";
        return CURLE_COULDNT_CONNECT;
    }
    return curl_easy_perform(curl);
}
