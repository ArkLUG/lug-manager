#pragma once
#include <string>
#include <cstdint>

struct Session {
    std::string token;
    int64_t     member_id  = 0;
    std::string role;
    std::string display_name;
    std::string expires_at;
    std::string created_at;
};
