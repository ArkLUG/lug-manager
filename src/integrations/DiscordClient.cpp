#include "integrations/DiscordClient.hpp"
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
    if (!guild_id.empty())                guild_id_                = guild_id;
    if (!lug_channel_id.empty())          lug_channel_id_          = lug_channel_id;
    if (!events_forum_channel_id.empty()) events_forum_channel_id_ = events_forum_channel_id;
    // Allow clearing these (empty = no ping)
    announcement_role_id_    = announcement_role_id;
    non_lug_event_role_id_   = non_lug_event_role_id;
    if (!timezone.empty())                timezone_                = timezone;
}

std::vector<DiscordChannel> DiscordClient::fetch_forum_channels() const {
    if (guild_id_.empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + guild_id_ + "/channels");
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
    if (guild_id_.empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + guild_id_ + "/channels");
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

std::string DiscordClient::discord_api_request(const std::string& method,
                                                const std::string& endpoint,
                                                const std::string& json_body) const {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string url = "https://discord.com/api/v10" + endpoint;
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

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!json_body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
    } else if (method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        if (!json_body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
        }
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (!json_body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    // GET is default

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("Discord API curl error: ") + curl_easy_strerror(res));
    }
    return response;
}

// Thread-safe conversion: interpret `iso` as local time in `tz_name` (IANA), return UTC ISO + tz abbreviation.
// Uses a global mutex around setenv/tzset to avoid races in multi-threaded context.
struct TzConvResult { std::string utc_iso; std::string abbrev; };
static std::mutex s_tz_mutex;
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
        std::lock_guard<std::mutex> lock(s_tz_mutex);
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
    return tz_convert(iso, timezone_).utc_iso;
}

std::string DiscordClient::build_meeting_event_json(const Meeting& m) const {
    json j;
    j["name"]            = m.title;
    j["description"]     = m.description;
    j["entity_type"]     = 3; // EXTERNAL
    j["entity_metadata"] = {{"location", m.location.empty() ? "TBD" : m.location}};
    j["scheduled_start_time"] = iso_to_discord_timestamp(m.start_time);
    j["scheduled_end_time"]   = iso_to_discord_timestamp(m.end_time);
    j["privacy_level"]   = 2; // GUILD_ONLY
    return j.dump();
}

std::string DiscordClient::build_lug_event_json(const LugEvent& e) const {
    json j;
    j["name"]            = e.title;
    j["description"]     = e.description;
    j["entity_type"]     = 3; // EXTERNAL
    j["entity_metadata"] = {{"location", e.location.empty() ? "TBD" : e.location}};
    j["scheduled_start_time"] = iso_to_discord_timestamp(e.start_time);
    j["scheduled_end_time"]   = iso_to_discord_timestamp(e.end_time);
    j["privacy_level"]   = 2; // GUILD_ONLY
    return j.dump();
}

// Extract city/state from full address for thread titles
static std::string shorten_location_dc(const std::string& loc) {
    std::vector<std::string> parts;
    std::istringstream ss(loc);
    std::string part;
    while (std::getline(ss, part, ',')) {
        size_t s = part.find_first_not_of(" \t");
        size_t e = part.find_last_not_of(" \t");
        if (s != std::string::npos) parts.push_back(part.substr(s, e - s + 1));
    }
    if (parts.size() < 3) return loc;

    auto is_country = [](const std::string& s) {
        if (s.size() <= 2) return false;
        for (char c : s) if (std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    };

    size_t last = parts.size() - 1;
    if (is_country(parts[last]) && parts.size() >= 4) last--;

    std::string state = parts[last];
    std::string city  = parts[last - 1];
    size_t sp = state.find(' ');
    if (sp != std::string::npos) state = state.substr(0, sp);
    return city + ", " + state;
}

static std::string format_lug_event_thread_name(const LugEvent& e) {
    auto fmt_date = [](const std::string& iso) -> std::string {
        if (iso.size() < 10) return iso;
        try {
            int month = std::stoi(iso.substr(5, 2));
            int day   = std::stoi(iso.substr(8, 2));
            std::string year = iso.substr(2, 2);
            if (month >= 1 && month <= 12)
                return std::to_string(month) + "/" + std::to_string(day) + "/" + year;
        } catch (...) {}
        return iso.substr(0, 10);
    };
    std::string d0 = fmt_date(e.start_time);
    std::string d1 = fmt_date(e.end_time);
    std::string date_part = (d0 == d1 || d1.empty()) ? d0 : d0 + "-" + d1;

    std::string name = e.title;
    if (!e.location.empty())   name += " | " + shorten_location_dc(e.location);
    if (!date_part.empty())    name += " | " + date_part;

    // Discord thread names max 100 characters
    if (name.size() > 100) name = name.substr(0, 100);
    return name;
}

// static
std::string DiscordClient::build_event_announcement_content(const LugEvent& e,
                                                              const std::string& role_id,
                                                              const std::string& thread_url,
                                                              bool suppress_pings) {
    auto fmt_date = [](const std::string& iso) -> std::string {
        if (iso.size() < 10) return iso;
        try {
            int month = std::stoi(iso.substr(5, 2));
            int day   = std::stoi(iso.substr(8, 2));
            if (month >= 1 && month <= 12)
                return std::to_string(month) + "/" + std::to_string(day);
        } catch (...) {}
        return iso.substr(0, 10);
    };

    // Role pings (suppressed if setting is on)
    std::string out;
    if (!suppress_pings) {
        if (!role_id.empty()) out += "<@&" + role_id + "> ";
        if (!e.discord_ping_role_ids.empty()) {
            std::istringstream ss(e.discord_ping_role_ids);
            std::string rid;
            while (std::getline(ss, rid, ','))
                if (!rid.empty()) out += "<@&" + rid + "> ";
        }
        if (!role_id.empty() || !e.discord_ping_role_ids.empty()) out += "\n";
    }

    if (e.scope == "non_lug") out += "[Non-LUG] ";
    out += "**" + e.title + "**\n";

    std::string d0 = fmt_date(e.start_time);
    std::string d1 = fmt_date(e.end_time);
    std::string date_str = (d0 == d1 || d1.empty()) ? d0 : d0 + " - " + d1;
    if (!date_str.empty()) out += "Dates: " + date_str + "\n";
    if (!e.location.empty()) out += "Location: " + e.location + "\n";
    if (!thread_url.empty()) out += "Discussion Thread: " + thread_url;
    return out;
}

// static
std::string DiscordClient::build_thread_starter_content(const LugEvent& e,
                                                          const std::string& role_id,
                                                          bool suppress_pings) {
    auto fmt_date = [](const std::string& iso) -> std::string {
        if (iso.size() < 10) return iso;
        try {
            int month = std::stoi(iso.substr(5, 2));
            int day   = std::stoi(iso.substr(8, 2));
            if (month >= 1 && month <= 12)
                return std::to_string(month) + "/" + std::to_string(day);
        } catch (...) {}
        return iso.substr(0, 10);
    };

    // Ping custom roles at the top (suppressed if setting is on)
    std::string out;
    if (!suppress_pings && !e.discord_ping_role_ids.empty()) {
        std::istringstream ss(e.discord_ping_role_ids);
        std::string rid;
        while (std::getline(ss, rid, ','))
            if (!rid.empty()) out += "<@&" + rid + "> ";
    }
    if (!out.empty()) out += "\n";

    if (e.scope == "non_lug") out += "[Non-LUG] ";
    out += "**" + e.title + "**\n";

    std::string d0 = fmt_date(e.start_time);
    std::string d1 = fmt_date(e.end_time);
    std::string date_str = (d0 == d1 || d1.empty()) ? d0 : d0 + " - " + d1;
    if (!date_str.empty()) out += "Dates: " + date_str + "\n";
    if (!e.location.empty()) out += "Location: " + e.location + "\n";

    // Lead field: Discord mention if available (unless pings suppressed), otherwise display name
    if (!suppress_pings && !e.event_lead_discord_id.empty())
        out += "Lead: <@" + e.event_lead_discord_id + ">\n";
    else if (!e.event_lead_name.empty())
        out += "Lead: " + e.event_lead_name + "\n";
    if (!e.signup_deadline.empty()) {
        std::string dl = fmt_date(e.signup_deadline);
        if (!dl.empty()) out += "Signup Deadline: " + dl + "\n";
    }
    if (e.max_attendees > 0)
        out += "Capacity: " + std::to_string(e.max_attendees) + "\n";
    if (!e.description.empty()) out += "\n" + e.description + "\n";
    return out;
}

// static
std::string DiscordClient::build_meeting_announcement_content(const Meeting& m,
                                                               const std::string& role_id,
                                                               const std::string& tz_name,
                                                               bool suppress_pings) {
    // Format datetime as "M/D H:MM AM/PM TZ" using DST-aware abbreviation
    auto fmt_datetime = [&tz_name](const std::string& iso) -> std::string {
        if (iso.size() < 16) return iso;
        try {
            int month = std::stoi(iso.substr(5, 2));
            int day   = std::stoi(iso.substr(8, 2));
            int hour  = std::stoi(iso.substr(11, 2));
            int min   = std::stoi(iso.substr(14, 2));
            std::string ampm = hour >= 12 ? "PM" : "AM";
            int h12 = hour % 12; if (h12 == 0) h12 = 12;
            char buf[32];
            snprintf(buf, sizeof(buf), "%d/%d %d:%02d %s", month, day, h12, min, ampm.c_str());
            std::string result = buf;
            // Append DST-aware timezone abbreviation
            if (!tz_name.empty() && tz_name != "UTC") {
                auto conv = tz_convert(iso, tz_name);
                if (!conv.abbrev.empty()) result += " " + conv.abbrev;
            } else if (tz_name == "UTC") {
                result += " UTC";
            }
            return result;
        } catch (...) {}
        return iso.substr(0, 16);
    };

    std::string out;
    if (!suppress_pings && !role_id.empty()) out += "<@&" + role_id + "> ";
    if (!out.empty()) out += "\n";

    out += "**" + m.title + "**\n";

    std::string d0 = fmt_datetime(m.start_time);
    std::string d1 = fmt_datetime(m.end_time);
    if (!d0.empty()) {
        out += "When: " + d0;
        if (!d1.empty() && d1 != d0) out += " – " + d1;
        out += "\n";
    }
    if (!m.location.empty()) out += "Where: " + m.location + "\n";
    if (!m.description.empty()) out += "\n" + m.description + "\n";
    return out;
}

std::string DiscordClient::sync_post_meeting_announcement(const std::string& channel_id,
                                                           const Meeting& m,
                                                           const std::string& role_id) {
    if (channel_id.empty()) return "";
    json body;
    body["content"] = build_meeting_announcement_content(m, role_id, timezone_, suppress_pings_);
    std::string resp = discord_api_request("POST", "/channels/" + channel_id + "/messages",
                                           body.dump());
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] sync_post_meeting_announcement missing 'id': " << resp << "\n";
    } catch (const json::exception& ex) {
        std::cerr << "[DiscordClient] sync_post_meeting_announcement parse error: " << ex.what() << "\n";
    }
    return "";
}

std::string DiscordClient::sync_create_forum_thread_for_event(const std::string& title,
                                                               const LugEvent& e) {
    if (events_forum_channel_id_.empty()) return "";
    std::string role_id = (e.scope == "non_lug") ? non_lug_event_role_id_ : announcement_role_id_;
    json body;
    body["name"]                  = title;
    body["auto_archive_duration"] = 10080; // 7 days
    body["message"]["content"]    = build_thread_starter_content(e, role_id, suppress_pings_);
    std::string resp = discord_api_request(
        "POST", "/channels/" + events_forum_channel_id_ + "/threads", body.dump());
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] sync_create_forum_thread_for_event missing 'id': " << resp << "\n";
    } catch (const json::exception& ex) {
        std::cerr << "[DiscordClient] sync_create_forum_thread_for_event parse error: " << ex.what() << "\n";
    }
    return "";
}

std::string DiscordClient::sync_create_text_thread_for_event(const std::string& title,
                                                              const LugEvent& e) {
    if (lug_channel_id_.empty()) return "";
    std::string role_id = (e.scope == "non_lug") ? non_lug_event_role_id_ : announcement_role_id_;
    // Step 1: post announcement message
    json msg_body;
    msg_body["content"] = build_event_announcement_content(e, role_id, "", suppress_pings_);
    std::string msg_resp = discord_api_request("POST",
                                               "/channels/" + lug_channel_id_ + "/messages",
                                               msg_body.dump());
    std::string message_id;
    try {
        auto j = json::parse(msg_resp);
        if (!j.contains("id")) {
            std::cerr << "[DiscordClient] sync_create_text_thread_for_event: message missing 'id': "
                      << msg_resp << "\n";
            return "";
        }
        message_id = j["id"].get<std::string>();
    } catch (const json::exception& ex) {
        std::cerr << "[DiscordClient] sync_create_text_thread_for_event parse error: " << ex.what() << "\n";
        return "";
    }
    // Step 2: create thread from message
    json thread_body;
    thread_body["name"]                  = title + " Discussion";
    thread_body["auto_archive_duration"] = 10080;
    std::string thread_resp = discord_api_request(
        "POST",
        "/channels/" + lug_channel_id_ + "/messages/" + message_id + "/threads",
        thread_body.dump());
    try {
        auto j = json::parse(thread_resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] sync_create_text_thread_for_event: thread missing 'id': "
                  << thread_resp << "\n";
    } catch (const json::exception& ex) {
        std::cerr << "[DiscordClient] sync_create_text_thread_for_event thread parse error: "
                  << ex.what() << "\n";
    }
    return "";
}

std::string DiscordClient::sync_post_event_announcement(const std::string& channel_id,
                                                          const LugEvent& e,
                                                          const std::string& role_id,
                                                          const std::string& thread_url) {
    if (channel_id.empty()) return "";
    json body;
    body["content"] = build_event_announcement_content(e, role_id, thread_url, suppress_pings_);
    std::string resp = discord_api_request("POST", "/channels/" + channel_id + "/messages",
                                           body.dump());
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] sync_post_event_announcement missing 'id': " << resp << "\n";
    } catch (const json::exception& ex) {
        std::cerr << "[DiscordClient] sync_post_event_announcement parse error: " << ex.what() << "\n";
    }
    return "";
}

std::string DiscordClient::sync_create_thread_from_message(const std::string& channel_id,
                                                             const std::string& message_id,
                                                             const std::string& thread_name) {
    if (channel_id.empty() || message_id.empty()) return "";
    json body;
    body["name"]                  = thread_name;
    body["auto_archive_duration"] = 10080; // 7 days
    std::string resp = discord_api_request(
        "POST",
        "/channels/" + channel_id + "/messages/" + message_id + "/threads",
        body.dump());
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] sync_create_thread_from_message missing 'id': " << resp << "\n";
    } catch (const json::exception& ex) {
        std::cerr << "[DiscordClient] sync_create_thread_from_message parse error: " << ex.what() << "\n";
    }
    return "";
}

void DiscordClient::delete_channel_message(const std::string& channel_id,
                                            const std::string& message_id) {
    if (channel_id.empty() || message_id.empty()) return;
    try {
        discord_api_request("DELETE",
            "/channels/" + channel_id + "/messages/" + message_id);
    } catch (const std::exception& ex) {
        std::cerr << "[DiscordClient] delete_channel_message failed (channel=" << channel_id
                  << " msg=" << message_id << "): " << ex.what() << "\n";
    }
}

void DiscordClient::update_channel_message(const std::string& channel_id,
                                            const std::string& message_id,
                                            const std::string& content) {
    if (channel_id.empty() || message_id.empty()) return;
    pool_.enqueue([this, channel_id, message_id, content]() {
        try {
            json body;
            body["content"] = content;
            discord_api_request("PATCH",
                "/channels/" + channel_id + "/messages/" + message_id,
                body.dump());
        } catch (const std::exception& ex) {
            std::cerr << "[DiscordClient] update_channel_message failed: " << ex.what() << "\n";
        }
    });
}

std::string DiscordClient::sync_create_scheduled_event_meeting(const Meeting& m) {
    std::string endpoint = "/guilds/" + guild_id_ + "/scheduled-events";
    std::string body = build_meeting_event_json(m);
    std::string resp = discord_api_request("POST", endpoint, body);
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] create_scheduled_event response missing 'id': " << resp << "\n";
        return "";
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] Failed to parse scheduled event response: " << e.what()
                  << " | body: " << resp << "\n";
        return "";
    }
}

std::string DiscordClient::sync_create_scheduled_event_event(const LugEvent& e) {
    std::string endpoint = "/guilds/" + guild_id_ + "/scheduled-events";
    std::string body = build_lug_event_json(e);
    std::string resp = discord_api_request("POST", endpoint, body);
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] create_event scheduled event response missing 'id': " << resp << "\n";
        return "";
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] Failed to parse LugEvent scheduled event response: " << e.what()
                  << " | body: " << resp << "\n";
        return "";
    }
}

std::string DiscordClient::sync_create_event_thread(const std::string& channel_id,
                                                     const std::string& title,
                                                     const std::string& description) {
    // Step 1: Post a message to the channel
    json msg_body;
    std::string msg_content;
    if (!suppress_pings_ && !announcement_role_id_.empty())
        msg_content = "<@&" + announcement_role_id_ + "> ";
    msg_content += "**" + title + "**\n" + description;
    msg_body["content"] = msg_content;
    std::string msg_resp = discord_api_request("POST",
                                               "/channels/" + channel_id + "/messages",
                                               msg_body.dump());
    std::string message_id;
    try {
        auto j = json::parse(msg_resp);
        if (!j.contains("id")) {
            std::cerr << "[DiscordClient] Post message response missing 'id': " << msg_resp << "\n";
            return "";
        }
        message_id = j["id"].get<std::string>();
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] Failed to parse message response: " << e.what()
                  << " | body: " << msg_resp << "\n";
        return "";
    }

    // Step 2: Create a thread from that message
    json thread_body;
    thread_body["name"]                  = title + " Discussion";
    thread_body["auto_archive_duration"] = 10080; // 7 days in minutes
    std::string thread_resp = discord_api_request(
        "POST",
        "/channels/" + channel_id + "/messages/" + message_id + "/threads",
        thread_body.dump());
    try {
        auto j = json::parse(thread_resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] Create thread response missing 'id': " << thread_resp << "\n";
        return "";
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] Failed to parse thread response: " << e.what()
                  << " | body: " << thread_resp << "\n";
        return "";
    }
}

std::vector<DiscordThread> DiscordClient::fetch_forum_threads() const {
    if (guild_id_.empty() || events_forum_channel_id_.empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + guild_id_ + "/threads/active");
    std::vector<DiscordThread> result;
    try {
        auto j = json::parse(resp);
        if (!j.contains("threads") || !j["threads"].is_array()) return result;
        for (auto& t : j["threads"]) {
            if (!t.contains("id") || !t.contains("name") || !t.contains("parent_id")) continue;
            if (t["parent_id"].get<std::string>() != events_forum_channel_id_) continue;
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

std::string DiscordClient::sync_create_forum_thread(const std::string& title,
                                                      const std::string& description) {
    if (events_forum_channel_id_.empty()) return "";
    json body;
    body["name"]                  = title;
    body["auto_archive_duration"] = 10080; // 7 days
    std::string content;
    if (!suppress_pings_ && !announcement_role_id_.empty())
        content = "<@&" + announcement_role_id_ + "> ";
    content += "**" + title + "**\n" + description;
    body["message"]["content"]    = content;
    std::string resp = discord_api_request(
        "POST", "/channels/" + events_forum_channel_id_ + "/threads", body.dump());
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[DiscordClient] sync_create_forum_thread response missing 'id': " << resp << "\n";
        return "";
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] sync_create_forum_thread parse error: " << e.what()
                  << " | body: " << resp << "\n";
        return "";
    }
}

std::vector<DiscordRole> DiscordClient::fetch_guild_roles() const {
    if (guild_id_.empty()) return {};
    std::string resp = discord_api_request("GET", "/guilds/" + guild_id_ + "/roles");
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
    if (guild_id_.empty() || discord_user_id.empty()) return {};
    std::string resp = discord_api_request("GET",
        "/guilds/" + guild_id_ + "/members/" + discord_user_id);
    std::vector<std::string> result;
    try {
        auto j = json::parse(resp);
        if (!j.contains("roles") || !j["roles"].is_array()) return result;
        for (auto& rid : j["roles"]) {
            result.push_back(rid.get<std::string>());
        }
    } catch (const json::exception& e) {
        std::cerr << "[DiscordClient] fetch_member_role_ids parse error: " << e.what()
                  << " | response: " << resp.substr(0, 200) << "\n";
    }
    return result;
}

void DiscordClient::add_member_role(const std::string& discord_user_id,
                                    const std::string& role_id) {
    if (guild_id_.empty() || discord_user_id.empty() || role_id.empty()) return;
    auto resp = discord_api_request("PUT",
        "/guilds/" + guild_id_ + "/members/" + discord_user_id + "/roles/" + role_id);
    // Success returns 204 No Content (empty body); any response body means an error
    if (!resp.empty()) {
        std::cerr << "[DiscordClient] add_member_role failed (user=" << discord_user_id
                  << " role=" << role_id << "): " << resp << "\n";
    }
}

void DiscordClient::remove_member_role(const std::string& discord_user_id,
                                       const std::string& role_id) {
    if (guild_id_.empty() || discord_user_id.empty() || role_id.empty()) return;
    auto resp = discord_api_request("DELETE",
        "/guilds/" + guild_id_ + "/members/" + discord_user_id + "/roles/" + role_id);
    // Success returns 204 No Content (empty body); any response body means an error
    if (!resp.empty()) {
        std::cerr << "[DiscordClient] remove_member_role failed (user=" << discord_user_id
                  << " role=" << role_id << "): " << resp << "\n";
    }
}

std::string DiscordClient::set_member_nickname(const std::string& discord_user_id,
                                                const std::string& nickname) {
    if (guild_id_.empty() || discord_user_id.empty()) return "skipped: empty id";
    json body;
    body["nick"] = nickname;

    for (int attempt = 0; attempt < 3; ++attempt) {
        auto resp = discord_api_request("PATCH",
            "/guilds/" + guild_id_ + "/members/" + discord_user_id, body.dump());
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
    if (guild_id_.empty() || discord_user_id.empty()) return;
    auto resp = discord_api_request("DELETE",
        "/guilds/" + guild_id_ + "/members/" + discord_user_id);
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
    if (guild_id_.empty()) return {};
    std::vector<DiscordGuildMember> result;
    std::string after = "0";

    while (true) {
        std::string endpoint = "/guilds/" + guild_id_ + "/members?limit=1000&after=" + after;
        std::string resp = discord_api_request("GET", endpoint);

        json j;
        try {
            j = json::parse(resp);
        } catch (const json::exception& e) {
            std::cerr << "[DiscordClient] fetch_guild_members parse error: " << e.what() << "\n";
            break;
        }
        if (!j.is_array() || j.empty()) break;

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

void DiscordClient::create_scheduled_event(const Meeting& m) {
    pool_.enqueue([this, m]() {
        try {
            std::string id = sync_create_scheduled_event_meeting(m);
            if (!id.empty()) {
                std::cout << "[DiscordClient] Created scheduled event for meeting '" << m.title
                          << "', discord_event_id=" << id << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[DiscordClient] create_scheduled_event failed: " << e.what() << "\n";
        }
    });
}

void DiscordClient::update_scheduled_event(const Meeting& m) {
    if (m.discord_event_id.empty()) {
        std::cerr << "[DiscordClient] update_scheduled_event: meeting has no discord_event_id\n";
        return;
    }
    pool_.enqueue([this, m]() {
        try {
            std::string endpoint = "/guilds/" + guild_id_ +
                                   "/scheduled-events/" + m.discord_event_id;
            std::string body = build_meeting_event_json(m);
            std::string resp = discord_api_request("PATCH", endpoint, body);
            std::cout << "[DiscordClient] Updated scheduled event " << m.discord_event_id
                      << " for meeting '" << m.title << "'\n";
        } catch (const std::exception& e) {
            std::cerr << "[DiscordClient] update_scheduled_event failed: " << e.what() << "\n";
        }
    });
}

void DiscordClient::cancel_scheduled_event(const std::string& discord_event_id) {
    if (discord_event_id.empty()) return;
    pool_.enqueue([this, discord_event_id]() {
        try {
            std::string endpoint = "/guilds/" + guild_id_ +
                                   "/scheduled-events/" + discord_event_id;
            json body;
            body["status"] = 4; // CANCELLED
            discord_api_request("PATCH", endpoint, body.dump());
            std::cout << "[DiscordClient] Cancelled scheduled event " << discord_event_id << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[DiscordClient] cancel_scheduled_event failed: " << e.what() << "\n";
        }
    });
}

void DiscordClient::delete_scheduled_event(const std::string& discord_event_id) {
    if (discord_event_id.empty()) return;
    pool_.enqueue([this, discord_event_id]() {
        try {
            discord_api_request("DELETE",
                "/guilds/" + guild_id_ + "/scheduled-events/" + discord_event_id);
            std::cout << "[DiscordClient] Deleted scheduled event " << discord_event_id << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[DiscordClient] delete_scheduled_event failed: " << e.what() << "\n";
        }
    });
}

void DiscordClient::delete_channel(const std::string& channel_or_thread_id) {
    if (channel_or_thread_id.empty()) return;
    pool_.enqueue([this, channel_or_thread_id]() {
        try {
            discord_api_request("DELETE", "/channels/" + channel_or_thread_id);
            std::cout << "[DiscordClient] Deleted channel/thread " << channel_or_thread_id << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[DiscordClient] delete_channel failed: " << e.what() << "\n";
        }
    });
}

void DiscordClient::create_event_thread(LugEvent& e) {
    // Sync call so we can fill e.discord_thread_id in place
    try {
        std::string thread_id = sync_create_event_thread(
            lug_channel_id_,
            e.title,
            e.description);
        e.discord_thread_id = thread_id;
        if (!thread_id.empty()) {
            std::cout << "[DiscordClient] Created thread " << thread_id
                      << " for event '" << e.title << "'\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "[DiscordClient] create_event_thread failed: " << ex.what() << "\n";
    }
}

void DiscordClient::create_event_scheduled_event(LugEvent& e) {
    // Sync call so we can fill e.discord_event_id in place
    try {
        std::string event_id = sync_create_scheduled_event_event(e);
        e.discord_event_id = event_id;
        if (!event_id.empty()) {
            std::cout << "[DiscordClient] Created scheduled event " << event_id
                      << " for LugEvent '" << e.title << "'\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "[DiscordClient] create_event_scheduled_event failed: " << ex.what() << "\n";
    }
}

void DiscordClient::update_event(const LugEvent& e) {
    pool_.enqueue([this, e]() {
        try {
            if (!e.discord_event_id.empty()) {
                std::string endpoint = "/guilds/" + guild_id_ +
                                       "/scheduled-events/" + e.discord_event_id;
                std::string body = build_lug_event_json(e);
                discord_api_request("PATCH", endpoint, body);
                std::cout << "[DiscordClient] Updated scheduled event " << e.discord_event_id
                          << " for LugEvent '" << e.title << "'\n";
            }
            if (!e.discord_thread_id.empty()) {
                // Rename the thread to match the updated event title/dates/location
                std::string new_thread_name = format_lug_event_thread_name(e);
                json rename_body;
                rename_body["name"] = new_thread_name;
                // Retry once after rate-limit delay
                for (int attempt = 0; attempt < 2; ++attempt) {
                    std::string rename_resp = discord_api_request("PATCH",
                        "/channels/" + e.discord_thread_id, rename_body.dump());
                    try {
                        auto rj = json::parse(rename_resp);
                        if (rj.contains("id")) {
                            std::cout << "[DiscordClient] Renamed thread " << e.discord_thread_id
                                      << " to '" << new_thread_name << "'\n";
                            break;
                        }
                        if (rj.contains("retry_after")) {
                            double wait_secs = rj["retry_after"].get<double>();
                            std::cerr << "[DiscordClient] Thread rename rate-limited, retrying in "
                                      << (int)wait_secs << "s\n";
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(static_cast<int>(wait_secs * 1000) + 500));
                        } else {
                            std::cerr << "[DiscordClient] Thread rename failed: " << rename_resp << "\n";
                            break;
                        }
                    } catch (...) {
                        std::cerr << "[DiscordClient] Thread rename response parse error: " << rename_resp << "\n";
                        break;
                    }
                }

                // For forum threads the starter message ID == thread ID; try to edit it.
                // For text channel threads this will fail — fall back to a new update post.
                std::string role = (e.scope == "non_lug") ? non_lug_event_role_id_ : announcement_role_id_;
                std::string new_content = build_thread_starter_content(e, role, suppress_pings_);
                json patch_body;
                patch_body["content"] = new_content;
                std::string patch_resp = discord_api_request(
                    "PATCH",
                    "/channels/" + e.discord_thread_id + "/messages/" + e.discord_thread_id,
                    patch_body.dump());
                bool patched = false;
                try {
                    auto j = json::parse(patch_resp);
                    patched = j.contains("id");
                } catch (...) {}

                if (!patched && !suppress_updates_) {
                    // Fallback: post an update message to the thread
                    json msg;
                    msg["content"] = "**Event Updated**\n" + new_content;
                    discord_api_request("POST",
                                        "/channels/" + e.discord_thread_id + "/messages",
                                        msg.dump());
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[DiscordClient] update_event failed: " << ex.what() << "\n";
        }
    });
}

void DiscordClient::post_message(const std::string& channel_id, const std::string& content) {
    pool_.enqueue([this, channel_id, content]() {
        try {
            json body;
            body["content"] = content;
            discord_api_request("POST", "/channels/" + channel_id + "/messages", body.dump());
        } catch (const std::exception& e) {
            std::cerr << "[DiscordClient] post_message failed: " << e.what() << "\n";
        }
    });
}
