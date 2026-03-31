#pragma once
#include "config/Config.hpp"
#include <string>

struct DiscordUserInfo {
    std::string id;
    std::string username;
    std::string global_name;
    std::string avatar;
};

class DiscordOAuth {
public:
    explicit DiscordOAuth(const Config& config);

    // Build the Discord OAuth2 authorization URL
    std::string get_auth_url(const std::string& state) const;

    // Exchange authorization code for access token
    // Returns access token string
    std::string exchange_code(const std::string& code) const;

    // Fetch Discord user info using access token
    DiscordUserInfo get_user_info(const std::string& access_token) const;

private:
    const Config& config_;
    static std::string url_encode(const std::string& str);
    static std::string http_get(const std::string& url, const std::string& auth_header);
    static std::string http_post_form(const std::string& url, const std::string& body);
};
