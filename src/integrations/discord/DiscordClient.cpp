#include "utils/Offline.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "utils/LocalTime.hpp"
#include "services/Features.hpp"
#include "utils/text/Utf8.hpp"
#include <regex>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <ctime>

using json = nlohmann::json;

size_t DiscordClient::write_cb(void* contents, size_t size, size_t nmemb, std::string* s) {
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

DiscordClient::DiscordClient(const Config& config, ThreadPool& pool)
    : config_(config), pool_(pool),
      guild_id_(config.discord_guild_id),
      lug_channel_id_(config.discord_announcements_channel_id) {}

void DiscordClient::reconfigure(const std::string& guild_id,
                                const std::string& lug_channel_id,
                                const std::string& events_forum_channel_id,
                                const std::string& announcement_role_id,
                                const std::string& non_lug_event_role_id,
                                const std::string& timezone) {
    clear_cache();
    std::lock_guard<std::mutex> l(cfg_mutex_);
    if (!guild_id.empty())                guild_id_                = guild_id;
    if (!lug_channel_id.empty())          lug_channel_id_          = lug_channel_id;
    if (!events_forum_channel_id.empty()) events_forum_channel_id_ = events_forum_channel_id;
    // Allow clearing these (empty = no ping)
    announcement_role_id_    = announcement_role_id;
    non_lug_event_role_id_   = non_lug_event_role_id;
    if (!timezone.empty())                timezone_                = timezone;
}

std::vector<DiscordChannel> DiscordClient::fetch_forum_channels() const {
    if (get_guild_id().empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + get_guild_id() + "/channels");
    std::vector<DiscordChannel> result;
    try {
        auto j = json::parse(resp);
        if (!j.is_array()) return result;
        for (auto& ch : j) {
            if (!ch.contains("id") || !ch.contains("name") || !ch.contains("type")) continue;
            if (ch["type"].get<int>() != 15) continue; // GUILD_FORUM
            DiscordChannel c;
            c.id   = ch["id"].get<std::string>();
            c.name = ch["name"].get<std::string>();
            c.type = 15;
            result.push_back(std::move(c));
        }
        std::sort(result.begin(), result.end(),
                  [](const DiscordChannel& a, const DiscordChannel& b) {
                      return a.name < b.name;
                  });
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] fetch_forum_channels parse error: " << e.what() << "\n";
    }
    return result;
}

std::vector<DiscordChannel> DiscordClient::fetch_text_channels() const {
    return fetch_text_channels(get_guild_id());
}

std::vector<DiscordChannel> DiscordClient::fetch_text_channels(const std::string& guild_id) const {
    if (guild_id.empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + guild_id + "/channels");
    std::vector<DiscordChannel> result;
    try {
        auto j = json::parse(resp);
        if (!j.is_array()) return result;
        for (auto& ch : j) {
            if (!ch.contains("id") || !ch.contains("name") || !ch.contains("type")) continue;
            int type = ch["type"].get<int>();
            if (type != 0 && type != 5) continue; // GUILD_TEXT (0) and GUILD_ANNOUNCEMENT (5)
            DiscordChannel c;
            c.id   = ch["id"].get<std::string>();
            c.name = ch["name"].get<std::string>();
            c.type = type;
            result.push_back(std::move(c));
        }
        std::sort(result.begin(), result.end(),
                  [](const DiscordChannel& a, const DiscordChannel& b) {
                      return a.name < b.name;
                  });
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] fetch_text_channels parse error: " << e.what() << "\n";
    }
    return result;
}

std::vector<DiscordChannel> DiscordClient::fetch_voice_channels() const {
    if (get_guild_id().empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + get_guild_id() + "/channels");
    std::vector<DiscordChannel> result;
    try {
        auto j = json::parse(resp);
        if (!j.is_array()) return result;
        for (auto& ch : j) {
            if (!ch.contains("id") || !ch.contains("name") || !ch.contains("type")) continue;
            int type = ch["type"].get<int>();
            if (type != 2 && type != 13) continue; // GUILD_VOICE (2) and GUILD_STAGE_VOICE (13)
            DiscordChannel c;
            c.id   = ch["id"].get<std::string>();
            c.name = ch["name"].get<std::string>();
            c.type = type;
            result.push_back(std::move(c));
        }
        std::sort(result.begin(), result.end(),
                  [](const DiscordChannel& a, const DiscordChannel& b) {
                      return a.name < b.name;
                  });
    } catch (const std::exception& e) {
        std::cerr << "[DiscordClient] fetch_voice_channels parse error: " << e.what() << "\n";
    }
    return result;
}

std::string DiscordClient::discord_api_request(const std::string& method,
                                                const std::string& endpoint,
                                                const std::string& json_body) const {
    // Cache guild-level list reads (channels, roles, active threads) briefly.
    static const std::regex cacheable(R"(^/guilds/\d+/(channels|roles|threads/active)$)");
    bool use_cache = method == "GET" && std::regex_match(endpoint, cacheable);
    if (use_cache) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = get_cache_.find(endpoint);
        if (it != get_cache_.end() && std::chrono::steady_clock::now() - it->second.first < std::chrono::minutes(2))
            return it->second.second;
    }
    std::string fresh = discord_api_request_uncached(method, endpoint, json_body);
    if (use_cache && !fresh.empty() && (fresh[0] == '[' || fresh.find("\"threads\"") != std::string::npos)) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        get_cache_[endpoint] = {std::chrono::steady_clock::now(), fresh};
    }
    return fresh;
}

void DiscordClient::clear_cache() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    get_cache_.clear();
}

std::string DiscordClient::discord_api_request_uncached(const std::string& method,
                                                       const std::string& endpoint,
                                                       const std::string& json_body) const {
    // Discord switched off (Settings > Features): nothing is sent at all.
    if (!Features::on("discord")) throw std::runtime_error("Discord is switched off");

    // Endpoints are built by concatenating ids that can originate from user
    // input (member discord_user_id via forms/API, channel/role ids from
    // settings). Refuse anything that could walk the URL path, so e.g. a
    // discord_user_id of "../../channels/<id>" can't steer a bot-authenticated
    // DELETE at a different Discord resource.
    for (size_t i = 0; i < endpoint.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(endpoint[i]);
        bool bad = c <= 0x20 || c == 0x7f || c == '\\' || c == '#' ||
                   endpoint.compare(i, 2, "..") == 0 ||
                   (c == '%' && (endpoint.compare(i, 3, "%2e") == 0 || endpoint.compare(i, 3, "%2E") == 0 ||
                                 endpoint.compare(i, 3, "%2f") == 0 || endpoint.compare(i, 3, "%2F") == 0));
        if (bad) throw std::runtime_error("Discord API: refusing unsafe endpoint path");
    }

    // Message text is built from member-supplied fields (event titles,
    // descriptions, display names). Without allowed_mentions Discord honors
    // any "@everyone"/"@here" in that text, letting anyone who can name an
    // event mass-ping the server. Role and user mentions (the app's own
    // announcement-role pings) are still allowed. Applies to every message
    // body we send (top-level content, or a forum thread's starter message).
    std::string body_to_send = json_body;
    if (!json_body.empty() && (method == "POST" || method == "PATCH")) {
        try {
            auto j = json::parse(json_body);
            auto guard = [](json& msg) {
                if (!msg.is_object() || !msg.contains("content") || !msg["content"].is_string()) return;
                if (!msg.contains("allowed_mentions"))
                    msg["allowed_mentions"] = {{"parse", json::array({"roles", "users"})}};
                // Over-long content (e.g. a big event description) is rejected
                // outright by Discord - post it truncated instead of not at all.
                msg["content"] = utf8_truncate(msg["content"].get<std::string>(), 2000);
            };
            guard(j);
            if (j.is_object() && j.contains("message")) guard(j["message"]);
            // Thread / scheduled-event names: Discord max 100 characters.
            if (j.is_object() && j.contains("name") && j["name"].is_string())
                j["name"] = utf8_truncate(j["name"].get<std::string>(), 100);
            body_to_send = j.dump();
        } catch (const json::exception&) {
            // Not JSON we built - send unchanged.
        }
    }

    // Discord answers 429 with {"retry_after": seconds} when rate limited;
    // bulk operations (sync-all, nickname sync) hit this routinely. Honor it
    // a few times instead of treating the error body as a success.
    for (int attempt = 0;; ++attempt) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");

        std::string url = discord_base() + "/api/v10" + endpoint;
        std::string response;

        struct curl_slist* headers = nullptr;
        std::string auth_header = "Authorization: Bot " + config_.discord_bot_token;
        headers = curl_slist_append(headers, auth_header.c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (!json_body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_to_send.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_to_send.size()));
            } else {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
            }
        } else if (method == "PATCH") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            if (!json_body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_to_send.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_to_send.size()));
            }
        } else if (method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (!json_body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_to_send.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_to_send.size()));
            }
        } else if (method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        // GET is default

        CURLcode res = guarded_perform(curl);
        curl_slist_free_all(headers);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("Discord API curl error: ") + curl_easy_strerror(res));
        }

        DiscordClient::last_status_ref() = http_code;
        if (http_code == 429 && attempt < 3) {
            double wait = 1.0;
            try {
                auto j = json::parse(response);
                if (j.contains("retry_after") && j["retry_after"].is_number())
                    wait = j["retry_after"].get<double>();
            } catch (...) {}
            if (wait > 10.0) wait = 10.0;
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(wait * 1000) + 100));
            continue;
        }
        return response;
    }
}

// Thread-safe conversion: interpret `iso` as local time in `tz_name` (IANA), return UTC ISO + tz abbreviation.
// Uses a global mutex around setenv/tzset to avoid races in multi-threaded context.
struct TzConvResult { std::string utc_iso; std::string abbrev; };
static TzConvResult tz_convert(const std::string& iso, const std::string& tz_name) {
    TzConvResult r;
    if (iso.size() < 16) { r.utc_iso = iso + "Z"; return r; }

    struct tm t = {};
    // Parse "YYYY-MM-DDTHH:MM" or "YYYY-MM-DDTHH:MM:SS"
    int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
    sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc);
    t.tm_year = yr - 1900; t.tm_mon = mo - 1; t.tm_mday = dy;
    t.tm_hour = hr; t.tm_min = mn; t.tm_sec = sc;
    t.tm_isdst = -1; // let the library determine DST

    time_t utc_t;
    {
        std::lock_guard<std::mutex> lock(tz_env_mutex());
        // Save and override TZ environment variable
        const char* old_env = getenv("TZ");
        std::string saved_tz = old_env ? old_env : "";
        bool had_tz = (old_env != nullptr);

        setenv("TZ", tz_name.empty() ? "UTC" : tz_name.c_str(), 1);
        tzset();

        utc_t = mktime(&t); // interprets t as local time in tz_name (DST-aware)

        // Grab the DST-resolved abbreviation (e.g. "CDT" or "CST")
        struct tm local_tm = {};
        localtime_r(&utc_t, &local_tm);
        if (local_tm.tm_zone) r.abbrev = local_tm.tm_zone;

        // Restore TZ
        if (had_tz) setenv("TZ", saved_tz.c_str(), 1);
        else        unsetenv("TZ");
        tzset();
    }

    struct tm utc_tm = {};
    gmtime_r(&utc_t, &utc_tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
    r.utc_iso = buf;
    return r;
}

std::string DiscordClient::iso_to_discord_timestamp(const std::string& iso) const {
    if (iso.empty()) return iso;
    // If already has explicit timezone (Z or ±HH:MM), pass through unchanged
    for (size_t i = 0; i < iso.size(); ++i) {
        if (iso[i] == 'Z') return iso;
        if (iso[i] == 'T') {
            for (size_t j = i + 1; j < iso.size(); ++j)
                if (iso[j] == '+' || (iso[j] == '-' && j > i + 1)) return iso;
            break;
        }
    }
    return tz_convert(iso, get_timezone()).utc_iso;
}





std::vector<DiscordThread> DiscordClient::fetch_forum_threads() const {
    if (get_guild_id().empty() || get_events_forum_channel_id().empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + get_guild_id() + "/threads/active");
    std::vector<DiscordThread> result;
    try {
        auto j = json::parse(resp);
        if (!j.contains("threads") || !j["threads"].is_array()) return result;
        for (auto& t : j["threads"]) {
            if (!t.contains("id") || !t.contains("name") || !t.contains("parent_id")) continue;
            if (t["parent_id"].get<std::string>() != get_events_forum_channel_id()) continue;
            DiscordThread dt;
            dt.id   = t["id"].get<std::string>();
            dt.name = t["name"].get<std::string>();
            result.push_back(std::move(dt));
        }
        std::sort(result.begin(), result.end(),
                  [](const DiscordThread& a, const DiscordThread& b) {
                      return a.name < b.name;
                  });
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] fetch_forum_threads parse error: " << e.what() << "\n";
    }
    return result;
}

std::vector<DiscordRole> DiscordClient::fetch_guild_roles() const {
    if (get_guild_id().empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + get_guild_id() + "/roles");
    std::vector<DiscordRole> result;
    try {
        auto j = json::parse(resp);
        if (!j.is_array()) return result;
        for (auto& r : j) {
            if (!r.contains("id") || !r.contains("name")) continue;
            std::string name = r["name"].get<std::string>();
            if (name == "@everyone") continue; // skip the default role
            DiscordRole dr;
            dr.id    = r["id"].get<std::string>();
            dr.name  = name;
            dr.color = r.value("color", 0);
            result.push_back(std::move(dr));
        }
        std::sort(result.begin(), result.end(),
                  [](const DiscordRole& a, const DiscordRole& b) {
                      return a.name < b.name;
                  });
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] fetch_guild_roles parse error: " << e.what() << "\n";
    }
    return result;
}

std::vector<std::string> DiscordClient::fetch_member_role_ids(const std::string& discord_user_id) const {
    return fetch_guild_member_role_ids(discord_user_id).value_or(std::vector<std::string>{});
}

std::optional<std::vector<std::string>>
DiscordClient::fetch_guild_member_role_ids(const std::string& discord_user_id) const {
    if (get_guild_id().empty() || discord_user_id.empty()) return std::nullopt;
    std::string resp = discord_api_request("GET",
        "/guilds/" + get_guild_id() + "/members/" + discord_user_id);
    try {
        auto j = json::parse(resp);
        // A guild member object always carries "user"; errors such as
        // Unknown Member (10007) come back as {"message":..,"code":..}.
        if (!j.is_object() || !j.contains("user") || !j.contains("roles") || !j["roles"].is_array())
            return std::nullopt;
        std::vector<std::string> result;
        for (auto& rid : j["roles"]) {
            result.push_back(rid.get<std::string>());
        }
        return result;
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] fetch_guild_member_role_ids parse error: " << e.what()
                  << " | response: " << resp.substr(0, 200) << "\n";
    }
    return std::nullopt;
}

void DiscordClient::add_member_role(const std::string& discord_user_id,
                                    const std::string& role_id) {
    if (get_guild_id().empty() || discord_user_id.empty() || role_id.empty()) return;
    auto resp = discord_api_request("PUT",
        "/guilds/" + get_guild_id() + "/members/" + discord_user_id + "/roles/" + role_id);
    // Success returns 204 No Content (empty body); any response body means an error
    if (!resp.empty()) {
        std::cerr << "[DiscordClient] add_member_role failed (user=" << discord_user_id
                  << " role=" << role_id << "): " << resp << "\n";
    }
}

void DiscordClient::remove_member_role(const std::string& discord_user_id,
                                       const std::string& role_id) {
    if (get_guild_id().empty() || discord_user_id.empty() || role_id.empty()) return;
    auto resp = discord_api_request("DELETE",
        "/guilds/" + get_guild_id() + "/members/" + discord_user_id + "/roles/" + role_id);
    // Success returns 204 No Content (empty body); any response body means an error
    if (!resp.empty()) {
        std::cerr << "[DiscordClient] remove_member_role failed (user=" << discord_user_id
                  << " role=" << role_id << "): " << resp << "\n";
    }
}

std::string DiscordClient::set_member_nickname(const std::string& discord_user_id,
                                                const std::string& nickname) {
    if (get_guild_id().empty() || discord_user_id.empty()) return "skipped: empty id";
    json body;
    body["nick"] = nickname;

    for (int attempt = 0; attempt < 3; ++attempt) {
        auto resp = discord_api_request("PATCH",
            "/guilds/" + get_guild_id() + "/members/" + discord_user_id, body.dump());
        try {
            auto j = json::parse(resp);
            if (j.contains("retry_after")) {
                // Rate limited — wait and retry
                double wait = j["retry_after"].get<double>();
                std::cerr << "[DiscordClient] set_member_nickname rate-limited (user=" << discord_user_id
                          << "), waiting " << wait << "s\n";
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>(wait * 1000) + 100));
                continue;
            }
            if (j.contains("code")) {
                int code = j["code"].get<int>();
                std::string msg = j.value("message", "unknown error");
                std::cerr << "[DiscordClient] set_member_nickname failed (user=" << discord_user_id
                          << " nick=" << nickname << "): " << msg << " (code " << code << ")\n";
                return msg + " (code " + std::to_string(code) + ")";
            }
        } catch (...) {}
        return ""; // success
    }
    return "rate limit exceeded after retries";
}

void DiscordClient::kick_member(const std::string& discord_user_id) {
    if (get_guild_id().empty() || discord_user_id.empty()) return;
    auto resp = discord_api_request("DELETE",
        "/guilds/" + get_guild_id() + "/members/" + discord_user_id);
    if (!resp.empty()) {
        try {
            auto j = json::parse(resp);
            if (j.contains("message")) {
                std::cerr << "[DiscordClient] kick_member failed (user=" << discord_user_id
                          << "): " << resp << "\n";
            }
        } catch (...) {}
    }
}

std::vector<DiscordGuildMember> DiscordClient::fetch_guild_members() const {
    if (get_guild_id().empty()) return {};
    std::vector<DiscordGuildMember> result;
    std::string after = "0";

    while (true) {
        std::string endpoint = "/guilds/" + get_guild_id() + "/members?limit=1000&after=" + after;
        std::string resp = discord_api_request("GET", endpoint);

        // A failed page (rate limit, permissions, outage) must fail the whole
        // fetch rather than silently return a partial list: the member sync
        // treats anyone missing from this list as having left the server.
        json j;
        try {
            j = json::parse(resp);
        } catch (const json::exception& e) {
            throw std::runtime_error(std::string("fetch_guild_members: unparseable response: ") + e.what());
        }
        if (!j.is_array())
            throw std::runtime_error("fetch_guild_members: Discord error: " + resp.substr(0, 200));
        if (j.empty()) break;

        std::string last_id;
        for (auto& gm : j) {
            if (!gm.contains("user")) continue;
            const auto& user = gm["user"];

            // Track last ID for pagination cursor (before skipping bots)
            if (user.contains("id")) last_id = user["id"].get<std::string>();

            // Skip bots
            if (user.contains("bot") && user["bot"].is_boolean() && user["bot"].get<bool>())
                continue;

            DiscordGuildMember m;
            m.discord_user_id = user.value("id", "");
            m.username        = user.value("username", "");
            m.global_name     = (user.contains("global_name") && !user["global_name"].is_null())
                                    ? user["global_name"].get<std::string>()
                                    : "";
            m.nick            = (gm.contains("nick") && !gm["nick"].is_null())
                                    ? gm["nick"].get<std::string>()
                                    : "";
            if (gm.contains("roles") && gm["roles"].is_array()) {
                for (auto& r : gm["roles"])
                    m.role_ids.push_back(r.get<std::string>());
            }
            if (!m.discord_user_id.empty())
                result.push_back(std::move(m));
        }

        if (static_cast<size_t>(j.size()) < 1000) break;
        if (last_id.empty()) break;
        after = last_id;
    }

    return result;
}

void DiscordClient::post_button_message(const std::string& channel_id, const std::string& content,
                                         const std::string& button_label, const std::string& custom_id) {
    pool_.enqueue([this, channel_id, content, button_label, custom_id]() {
        try {
            json button;
            button["type"]      = 2; // Button
            button["style"]     = 1; // Primary (blurple)
            button["label"]     = button_label;
            button["custom_id"] = custom_id;

            json row;
            row["type"]       = 1; // Action Row (container)
            row["components"] = json::array({button});

            json body;
            body["content"]    = content;
            body["components"] = json::array({row});
            discord_api_request("POST", "/channels/" + channel_id + "/messages", body.dump());
        } catch (const std::exception& e) {
            std::cerr << "[DiscordClient] post_button_message failed: " << e.what() << "\n";
        }
    });
}

bool DiscordClient::send_dm(const std::string& discord_user_id, const std::string& content) {
    return direct_message(discord_user_id, content).ok;
}

std::time_t DiscordClient::local_to_epoch(const std::string& iso, const std::string& tz_name) {
    if (iso.size() < 16) return -1;
    std::string utc = tz_convert(iso, tz_name).utc_iso; // "YYYY-MM-DDTHH:MM:SSZ"
    std::tm t{};
    if (sscanf(utc.c_str(), "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return -1;
    t.tm_year -= 1900;
    t.tm_mon  -= 1;
    return timegm(&t);
}

std::string DiscordClient::friendly_time(const std::string& iso, const std::string& tz_name) {
    if (iso.size() < 16) return iso;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5) return iso;
    std::tm t{};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = 12;
    timegm(&t); // normalizes and fills tm_wday
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %d/%d %d:%02d %s", days[t.tm_wday], mo, d,
                  h % 12 == 0 ? 12 : h % 12, mi, h >= 12 ? "PM" : "AM");
    std::string out = buf;
    std::string abbrev = tz_convert(iso, tz_name).abbrev;
    if (!abbrev.empty()) out += " " + abbrev;
    return out;
}

// ── Maintenance helpers (DiscordTimeRepair) ──

std::string DiscordClient::sync_get(const std::string& endpoint) const {
    return discord_api_request_uncached("GET", endpoint, "");
}

std::string DiscordClient::sync_patch_scheduled_event_times(const std::string& discord_event_id,
                                                            const std::string& start_utc, const std::string& end_utc) {
    json body;
    body["scheduled_start_time"] = start_utc;
    if (!end_utc.empty()) body["scheduled_end_time"] = end_utc;
    return discord_api_request_uncached("PATCH", "/guilds/" + get_guild_id() + "/scheduled-events/" + discord_event_id,
                                        body.dump());
}

std::string DiscordClient::sync_edit_message_content(const std::string& channel_id, const std::string& message_id,
                                                     const std::string& content) {
    json body;
    body["content"] = content;
    body["allowed_mentions"] = {{"parse", json::array()}};
    return discord_api_request_uncached("PATCH", "/channels/" + channel_id + "/messages/" + message_id, body.dump());
}

std::string DiscordClient::tz_abbrev(const std::string& local_iso, const std::string& tz_name) {
    return tz_convert(local_iso, tz_name).abbrev;
}

// ── Generic operations (chat::DiscordProvider) ──

long& DiscordClient::last_status_ref() {
    static thread_local long status = 0;
    return status;
}

namespace {
json mentions_json(const std::vector<std::string>& roles, const std::vector<std::string>& users) {
    json am;
    am["parse"] = json::array();
    am["roles"] = json::array();
    am["users"] = json::array();
    for (const auto& r : roles) if (!r.empty()) am["roles"].push_back(r);
    for (const auto& u : users) if (!u.empty()) am["users"].push_back(u);
    return am;
}
DiscordClient::Result result_of(const std::string& resp, long status, bool want_id) {
    DiscordClient::Result r;
    json j = json::parse(resp, nullptr, false);
    bool ok_status = status >= 200 && status < 300;
    if (ok_status && (!want_id || (j.is_object() && j.contains("id") && j["id"].is_string()))) {
        r.ok = true;
        if (j.is_object() && j.contains("id") && j["id"].is_string()) r.id = j["id"].get<std::string>();
        return r;
    }
    r.error = "HTTP " + std::to_string(status);
    if (j.is_object() && j.contains("message") && j["message"].is_string()) r.error += ": " + j["message"].get<std::string>();
    return r;
}
}

DiscordClient::Result DiscordClient::call(const std::string& method, const std::string& endpoint, const std::string& body, bool want_id) {
    try {
        std::string resp = discord_api_request_uncached(method, endpoint, body);
        return result_of(resp, last_status_ref(), want_id);
    } catch (const std::exception& e) {
        Result r;
        r.error = e.what();
        return r;
    }
}

DiscordClient::Result DiscordClient::send_message(const std::string& channel, const std::string& content,
                                                  const std::vector<std::string>& roles, const std::vector<std::string>& users) {
    if (channel.empty()) return Result{false, "", "no channel"};
    json body;
    body["content"] = content;
    body["allowed_mentions"] = mentions_json(roles, users);
    return call("POST", "/channels/" + channel + "/messages", body.dump(), true);
}

DiscordClient::Result DiscordClient::edit_message(const std::string& channel, const std::string& message, const std::string& content,
                                                  const std::vector<std::string>& roles, const std::vector<std::string>& users) {
    if (channel.empty() || message.empty()) return Result{false, "", "nothing to edit"};
    json body;
    body["content"] = content;
    body["allowed_mentions"] = mentions_json(roles, users);
    return call("PATCH", "/channels/" + channel + "/messages/" + message, body.dump(), false);
}

DiscordClient::Result DiscordClient::remove_message(const std::string& channel, const std::string& message) {
    if (channel.empty() || message.empty()) return Result{false, "", "nothing to delete"};
    return call("DELETE", "/channels/" + channel + "/messages/" + message, "", false);
}

DiscordClient::Result DiscordClient::start_forum_thread(const std::string& forum, const std::string& name, const std::string& content,
                                                        const std::vector<std::string>& roles, const std::vector<std::string>& users) {
    if (forum.empty()) return Result{false, "", "no forum channel"};
    json body;
    body["name"] = utf8_truncate(name, 100);
    body["auto_archive_duration"] = 10080;   // 7 days
    body["message"]["content"] = content;
    body["message"]["allowed_mentions"] = mentions_json(roles, users);
    return call("POST", "/channels/" + forum + "/threads", body.dump(), true);
}

DiscordClient::Result DiscordClient::start_thread_from_message(const std::string& channel, const std::string& message, const std::string& name) {
    if (channel.empty() || message.empty()) return Result{false, "", "no message"};
    json body;
    body["name"] = utf8_truncate(name, 100);
    body["auto_archive_duration"] = 10080;
    return call("POST", "/channels/" + channel + "/messages/" + message + "/threads", body.dump(), true);
}

DiscordClient::Result DiscordClient::rename_thread(const std::string& thread, const std::string& name) {
    if (thread.empty()) return Result{false, "", "no thread"};
    json body;
    body["name"] = utf8_truncate(name, 100);
    return call("PATCH", "/channels/" + thread, body.dump(), false);
}

DiscordClient::Result DiscordClient::remove_channel(const std::string& id) {
    if (id.empty()) return Result{false, "", "no channel"};
    return call("DELETE", "/channels/" + id, "", false);
}

std::string DiscordClient::scheduled_json(const ScheduledEvent& e) const {
    json j;
    j["name"]            = utf8_truncate(e.name, 100);
    j["description"]     = utf8_truncate(e.description, 1000);
    j["entity_type"]     = 3; // EXTERNAL
    j["entity_metadata"] = {{"location", e.location.empty() ? "TBD" : utf8_truncate(e.location, 100)}};
    j["scheduled_start_time"] = iso_to_discord_timestamp(e.start_local);
    j["scheduled_end_time"]   = iso_to_discord_timestamp(e.end_local.empty() ? e.start_local : e.end_local);
    j["privacy_level"]   = 2; // GUILD_ONLY
    return j.dump();
}

DiscordClient::Result DiscordClient::create_scheduled(const ScheduledEvent& e) {
    if (get_guild_id().empty()) return Result{false, "", "no server"};
    return call("POST", "/guilds/" + get_guild_id() + "/scheduled-events", scheduled_json(e), true);
}

DiscordClient::Result DiscordClient::update_scheduled(const std::string& id, const ScheduledEvent& e) {
    if (get_guild_id().empty() || id.empty()) return Result{false, "", "nothing to update"};
    return call("PATCH", "/guilds/" + get_guild_id() + "/scheduled-events/" + id, scheduled_json(e), false);
}

DiscordClient::Result DiscordClient::remove_scheduled(const std::string& id) {
    if (get_guild_id().empty() || id.empty()) return Result{false, "", "nothing to delete"};
    return call("DELETE", "/guilds/" + get_guild_id() + "/scheduled-events/" + id, "", false);
}

DiscordClient::Result DiscordClient::direct_message(const std::string& user, const std::string& content) {
    if (user.empty()) return Result{false, "", "no Discord account"};
    json open;
    open["recipient_id"] = user;
    Result ch = call("POST", "/users/@me/channels", open.dump(), true);
    if (!ch.ok) return ch;
    return send_message(ch.id, content, {}, {});
}

DiscordClient::Result DiscordClient::direct_message(const std::string& user, const std::string& content,
                                                    const std::string& components_json) {
    if (user.empty()) return Result{false, "", "no Discord account"};
    json open;
    open["recipient_id"] = user;
    Result ch = call("POST", "/users/@me/channels", open.dump(), true);
    if (!ch.ok) return ch;
    json body;
    body["content"] = content;
    body["allowed_mentions"] = mentions_json({}, {});
    try { body["components"] = json::parse(components_json); } catch (...) {}
    return call("POST", "/channels/" + ch.id + "/messages", body.dump(), true);
}

void DiscordClient::run_async(std::function<void()> job) {
    pool_.enqueue([job = std::move(job)]() {
        try { job(); } catch (const std::exception& e) { std::cerr << "[async] " << e.what() << "\n"; } catch (...) {}
    });
}
