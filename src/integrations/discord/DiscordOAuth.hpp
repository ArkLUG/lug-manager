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

    // Build the Discord OAuth2 authorization URL. skip_prompt=true (default)
    // adds prompt=none, so a user who has already authorized this app with
    // this scope is redirected straight back with a code instead of seeing
    // the consent screen again on every login. If the user has never
    // authorized (or revoked access), Discord redirects back with
    // error=interaction_required instead of a code — the caller (see
    // AuthRoutes.cpp's /auth/callback) retries once with skip_prompt=false
    // so first-time/revoked users still see a normal consent screen rather
    // than being stuck.
    std::string get_auth_url(const std::string& state, const std::string& redirect_uri = "",
                              bool skip_prompt = true) const;

    // Exchange authorization code for access token
    // Returns access token string
    std::string exchange_code(const std::string& code, const std::string& redirect_uri = "") const;

    // Fetch Discord user info using access token
    DiscordUserInfo get_user_info(const std::string& access_token) const;

private:
    const Config& config_;
    static std::string url_encode(const std::string& str);
    static std::string http_get(const std::string& url, const std::string& auth_header);
    static std::string http_post_form(const std::string& url, const std::string& body);
};
