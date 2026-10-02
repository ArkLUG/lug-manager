#pragma once
#include <string>
#include <cstdint>

struct Session {
    std::string token;
    int64_t     member_id  = 0;
    std::string role;
    std::string display_name;
    bool        treasurer = false;   // members.is_treasurer, re-read on every lookup
    std::string expires_at;
    std::string created_at;
    bool        renewed = false;     // this lookup extended it (the cookie should be re-sent)
};
