#pragma once
#include <crow.h>
#include <string>

// The visitor's address: the last X-Forwarded-For entry (the one our reverse
// proxy appended - earlier entries can be made up by the client), else the
// socket address.
inline std::string client_ip(const crow::request& req) {
    std::string xff = req.get_header_value("X-Forwarded-For");
    if (xff.empty()) return req.remote_ip_address;
    std::string last = xff.substr(xff.rfind(',') == std::string::npos ? 0 : xff.rfind(',') + 1);
    last.erase(0, last.find_first_not_of(' '));
    last.erase(last.find_last_not_of(' ') + 1);
    return last.empty() ? req.remote_ip_address : last.substr(0, 64);
}
