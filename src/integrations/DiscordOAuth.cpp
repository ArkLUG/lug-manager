#include "integrations/DiscordOAuth.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using json = nlohmann::json;

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, std::string* s) {
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

DiscordOAuth::DiscordOAuth(const Config& config) : config_(config) {}

std::string DiscordOAuth::url_encode(const std::string& str) {
    std::ostringstream oss;
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

std::string DiscordOAuth::get_auth_url(const std::string& state) const {
    return "https://discord.com/oauth2/authorize"
           "?client_id=" + url_encode(config_.discord_client_id) +
           "&redirect_uri=" + url_encode(config_.discord_redirect_uri) +
           "&response_type=code"
           "&scope=identify"
           "&state=" + url_encode(state);
}

std::string DiscordOAuth::http_post_form(const std::string& url, const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
    return response;
}

std::string DiscordOAuth::http_get(const std::string& url, const std::string& auth_header) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    std::string response;
    struct curl_slist* headers = nullptr;
    if (!auth_header.empty()) headers = curl_slist_append(headers, auth_header.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    CURLcode res = curl_easy_perform(curl);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
    return response;
}

std::string DiscordOAuth::exchange_code(const std::string& code) const {
    std::string body =
        "client_id=" + url_encode(config_.discord_client_id) +
        "&client_secret=" + url_encode(config_.discord_client_secret) +
        "&grant_type=authorization_code" +
        "&code=" + url_encode(code) +
        "&redirect_uri=" + url_encode(config_.discord_redirect_uri);
    std::string resp = http_post_form("https://discord.com/api/oauth2/token", body);
    try {
        auto j = json::parse(resp);
        if (j.contains("error")) {
            throw std::runtime_error("Discord OAuth error: " + j.value("error_description", j["error"].get<std::string>()));
        }
        return j["access_token"].get<std::string>();
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Failed to parse token response: ") + e.what() + " | body: " + resp);
    }
}

DiscordUserInfo DiscordOAuth::get_user_info(const std::string& access_token) const {
    std::string resp = http_get("https://discord.com/api/users/@me",
                                "Authorization: Bearer " + access_token);
    try {
        auto j = json::parse(resp);
        DiscordUserInfo info;
        info.id          = j.value("id", "");
        info.username    = j.value("username", "");
        info.global_name = (j.contains("global_name") && !j["global_name"].is_null())
                               ? j["global_name"].get<std::string>()
                               : info.username;
        info.avatar      = j.value("avatar", "");
        return info;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Failed to parse user info: ") + e.what());
    }
}
