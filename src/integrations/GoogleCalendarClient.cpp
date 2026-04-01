#include "integrations/GoogleCalendarClient.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>

using json = nlohmann::json;

GoogleCalendarClient::GoogleCalendarClient() {}

size_t GoogleCalendarClient::write_cb(void* contents, size_t size, size_t nmemb, std::string* s) {
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

std::string GoogleCalendarClient::base64url_encode(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string result;
    result.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        if (i + 1 < len) result += table[(n >> 6) & 0x3F];
        if (i + 2 < len) result += table[n & 0x3F];
    }
    return result;
}

std::string GoogleCalendarClient::url_encode(const std::string& s) {
    CURL* curl = curl_easy_init();
    if (!curl) return s;
    char* encoded = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.size()));
    std::string result = encoded ? encoded : s;
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

void GoogleCalendarClient::reconfigure(const std::string& service_account_json_path,
                                        const std::string& calendar_id,
                                        const std::string& timezone) {
    service_account_json_path_ = service_account_json_path;
    calendar_id_ = calendar_id;
    if (!timezone.empty()) timezone_ = timezone;
    configured_ = false;
    {
        std::lock_guard<std::mutex> lock(token_mutex_);
        access_token_.clear();
        token_expiry_ = 0;
    }
    if (!service_account_json_path_.empty() && !calendar_id_.empty()) {
        try {
            load_service_account();
        } catch (const std::exception& ex) {
            std::cerr << "[GoogleCalendar] Failed to load service account: " << ex.what() << "\n";
        }
    }
}

bool GoogleCalendarClient::is_configured() const {
    return configured_;
}

void GoogleCalendarClient::load_service_account() {
    std::ifstream f(service_account_json_path_);
    if (!f.is_open()) {
        std::cerr << "[GoogleCalendar] Cannot open service account file: "
                  << service_account_json_path_ << "\n";
        return;
    }
    try {
        json j = json::parse(f);
        sa_client_email_ = j.value("client_email", "");
        sa_private_key_  = j.value("private_key", "");
        sa_token_uri_    = j.value("token_uri", "https://oauth2.googleapis.com/token");
        if (sa_client_email_.empty() || sa_private_key_.empty()) {
            std::cerr << "[GoogleCalendar] Service account JSON missing client_email or private_key\n";
            return;
        }
        configured_ = true;
        std::cout << "[GoogleCalendar] Configured: " << sa_client_email_
                  << " → calendar " << calendar_id_ << "\n";
    } catch (const json::exception& ex) {
        std::cerr << "[GoogleCalendar] Failed to parse service account JSON: " << ex.what() << "\n";
    }
}

std::string GoogleCalendarClient::build_jwt() const {
    // Header
    json header;
    header["alg"] = "RS256";
    header["typ"] = "JWT";
    std::string h = header.dump();

    // Claims
    time_t now = time(nullptr);
    json claims;
    claims["iss"]   = sa_client_email_;
    claims["scope"] = "https://www.googleapis.com/auth/calendar";
    claims["aud"]   = sa_token_uri_;
    claims["iat"]   = now;
    claims["exp"]   = now + 3600;
    std::string c = claims.dump();

    std::string h_enc = base64url_encode(reinterpret_cast<const unsigned char*>(h.data()), h.size());
    std::string c_enc = base64url_encode(reinterpret_cast<const unsigned char*>(c.data()), c.size());

    return h_enc + "." + c_enc;
}

std::string GoogleCalendarClient::sign_jwt(const std::string& header_payload) const {
    // Load RSA private key from PEM string
    BIO* bio = BIO_new_mem_buf(sa_private_key_.data(), static_cast<int>(sa_private_key_.size()));
    if (!bio) throw std::runtime_error("BIO_new_mem_buf failed");

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) throw std::runtime_error("PEM_read_bio_PrivateKey failed");

    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) { EVP_PKEY_free(pkey); throw std::runtime_error("EVP_MD_CTX_new failed"); }

    size_t sig_len = 0;
    std::string signature;

    if (EVP_DigestSignInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1 ||
        EVP_DigestSignUpdate(md_ctx, header_payload.data(), header_payload.size()) != 1 ||
        EVP_DigestSignFinal(md_ctx, nullptr, &sig_len) != 1) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestSign failed");
    }

    std::vector<unsigned char> sig_buf(sig_len);
    if (EVP_DigestSignFinal(md_ctx, sig_buf.data(), &sig_len) != 1) {
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestSignFinal failed");
    }

    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);

    return base64url_encode(sig_buf.data(), sig_len);
}

std::string GoogleCalendarClient::ensure_access_token() const {
    std::lock_guard<std::mutex> lock(token_mutex_);

    time_t now = time(nullptr);
    if (!access_token_.empty() && now < token_expiry_ - 60)
        return access_token_;

    // Build and sign JWT
    std::string header_payload = build_jwt();
    std::string sig = sign_jwt(header_payload);
    std::string assertion = header_payload + "." + sig;

    // Exchange for access token
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string post_data = "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=" + assertion;
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, sa_token_uri_.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("Token request failed: ") + curl_easy_strerror(res));

    try {
        auto j = json::parse(response);
        if (!j.contains("access_token"))
            throw std::runtime_error("Token response missing access_token: " + response);
        access_token_ = j["access_token"].get<std::string>();
        int expires_in = j.value("expires_in", 3600);
        token_expiry_ = now + expires_in;
    } catch (const json::exception& ex) {
        throw std::runtime_error(std::string("Token parse error: ") + ex.what());
    }

    return access_token_;
}

std::string GoogleCalendarClient::gcal_api_request(const std::string& method,
                                                    const std::string& endpoint,
                                                    const std::string& json_body) const {
    std::string token = ensure_access_token();

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string url = "https://www.googleapis.com/calendar/v3" + endpoint;
    std::string response;

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    } else if (method == "PUT" || method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("Google Calendar API error: ") + curl_easy_strerror(res));

    if (http_code >= 400) {
        std::cerr << "[GoogleCalendar] API error " << http_code << ": " << response << "\n";
    }

    return response;
}

std::string GoogleCalendarClient::build_event_json(const std::string& title,
                                                    const std::string& description,
                                                    const std::string& location,
                                                    const std::string& start_time,
                                                    const std::string& end_time,
                                                    const std::string& status,
                                                    bool all_day) const {
    json j;
    j["summary"] = title;
    if (!description.empty()) j["description"] = description;
    if (!location.empty())    j["location"]    = location;

    // Google Calendar status: "confirmed", "tentative", or "cancelled"
    if (status == "tentative")       j["status"] = "tentative";
    else if (status == "cancelled")  j["status"] = "cancelled";
    else                             j["status"] = "confirmed";

    if (all_day) {
        // All-day events use "date" field (YYYY-MM-DD), not "dateTime"
        // Google Calendar end date is EXCLUSIVE — add 1 day so a single-day event renders correctly
        auto to_date = [](const std::string& dt) -> std::string {
            return dt.size() >= 10 ? dt.substr(0, 10) : dt;
        };
        auto next_day = [](const std::string& date_str) -> std::string {
            if (date_str.size() < 10) return date_str;
            try {
                int y = std::stoi(date_str.substr(0, 4));
                int m = std::stoi(date_str.substr(5, 2));
                int d = std::stoi(date_str.substr(8, 2));
                // Simple day increment (handles month/year rollover via mktime)
                struct tm t = {};
                t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d + 1;
                t.tm_isdst = -1;
                mktime(&t);
                char buf[11];
                strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
                return buf;
            } catch (...) {}
            return date_str;
        };
        std::string start_date = to_date(start_time);
        std::string end_date   = to_date(!end_time.empty() ? end_time : start_time);
        j["start"]["date"] = start_date;
        j["end"]["date"]   = next_day(end_date);
    } else {
        std::string tz = timezone_.empty() ? "UTC" : timezone_;
        j["start"]["dateTime"] = start_time;
        j["start"]["timeZone"] = tz;
        if (!end_time.empty()) {
            j["end"]["dateTime"] = end_time;
            j["end"]["timeZone"] = tz;
        } else {
            j["end"]["dateTime"] = start_time;
            j["end"]["timeZone"] = tz;
        }
    }

    return j.dump();
}

std::string GoogleCalendarClient::create_event(const Meeting& m) {
    if (!configured_) return "";
    std::string body = build_event_json(m.title, m.description, m.location, m.start_time, m.end_time, m.status);
    std::string resp = gcal_api_request("POST",
        "/calendars/" + url_encode(calendar_id_) + "/events", body);
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[GoogleCalendar] create_event missing 'id': " << resp << "\n";
    } catch (const json::exception& ex) {
        std::cerr << "[GoogleCalendar] create_event parse error: " << ex.what() << "\n";
    }
    return "";
}

std::string GoogleCalendarClient::create_event(const LugEvent& e) {
    if (!configured_) return "";
    std::string body = build_event_json(e.title, e.description, e.location, e.start_time, e.end_time, e.status, true);
    std::string resp = gcal_api_request("POST",
        "/calendars/" + url_encode(calendar_id_) + "/events", body);
    try {
        auto j = json::parse(resp);
        if (j.contains("id")) return j["id"].get<std::string>();
        std::cerr << "[GoogleCalendar] create_event missing 'id': " << resp << "\n";
    } catch (const json::exception& ex) {
        std::cerr << "[GoogleCalendar] create_event parse error: " << ex.what() << "\n";
    }
    return "";
}

void GoogleCalendarClient::update_event(const std::string& gcal_event_id, const Meeting& m) {
    if (!configured_ || gcal_event_id.empty()) return;
    std::string body = build_event_json(m.title, m.description, m.location, m.start_time, m.end_time, m.status);
    gcal_api_request("PUT",
        "/calendars/" + url_encode(calendar_id_) + "/events/" + url_encode(gcal_event_id), body);
}

void GoogleCalendarClient::update_event(const std::string& gcal_event_id, const LugEvent& e) {
    if (!configured_ || gcal_event_id.empty()) return;
    std::string body = build_event_json(e.title, e.description, e.location, e.start_time, e.end_time, e.status, true);
    gcal_api_request("PUT",
        "/calendars/" + url_encode(calendar_id_) + "/events/" + url_encode(gcal_event_id), body);
}

std::vector<GCalImportedEvent> GoogleCalendarClient::fetch_upcoming_events(int max_results) {
    std::vector<GCalImportedEvent> result;
    if (!configured_) return result;

    // Build timeMin as current UTC time in RFC 3339
    time_t now = time(nullptr);
    struct tm utc = {};
    gmtime_r(&now, &utc);
    char time_min[32];
    strftime(time_min, sizeof(time_min), "%Y-%m-%dT%H:%M:%SZ", &utc);

    std::string endpoint = "/calendars/" + url_encode(calendar_id_) + "/events"
        "?timeMin=" + url_encode(time_min) +
        "&maxResults=" + std::to_string(max_results) +
        "&singleEvents=true&orderBy=startTime";

    try {
        std::string resp = gcal_api_request("GET", endpoint);
        auto j = json::parse(resp);
        if (!j.contains("items") || !j["items"].is_array()) return result;

        for (auto& item : j["items"]) {
            GCalImportedEvent ev;
            ev.google_id   = item.value("id", "");
            ev.title       = item.value("summary", "");
            ev.description = item.value("description", "");
            ev.location    = item.value("location", "");

            // Start time: dateTime = timed event, date = all-day event
            if (item.contains("start")) {
                if (item["start"].contains("dateTime"))
                    ev.start_time = item["start"]["dateTime"].get<std::string>();
                else if (item["start"].contains("date")) {
                    ev.start_time = item["start"]["date"].get<std::string>() + "T00:00:00";
                    ev.is_all_day = true;
                }
            }
            if (item.contains("end")) {
                if (item["end"].contains("dateTime"))
                    ev.end_time = item["end"]["dateTime"].get<std::string>();
                else if (item["end"].contains("date"))
                    ev.end_time = item["end"]["date"].get<std::string>() + "T00:00:00";
            }

            // Strip timezone offset from dateTime (e.g. "2026-04-15T19:00:00-05:00" → "2026-04-15T19:00:00")
            // We keep local times since the timezone is already known from settings
            auto strip_tz = [](std::string& dt) {
                if (dt.size() > 19) {
                    // Check for +/-HH:MM or Z at the end
                    size_t t_pos = dt.find('T');
                    if (t_pos != std::string::npos) {
                        for (size_t i = t_pos + 1; i < dt.size(); ++i) {
                            if (dt[i] == '+' || dt[i] == '-' || dt[i] == 'Z') {
                                dt = dt.substr(0, i);
                                break;
                            }
                        }
                    }
                }
            };
            strip_tz(ev.start_time);
            strip_tz(ev.end_time);

            if (!ev.title.empty()) result.push_back(std::move(ev));
        }
    } catch (const std::exception& ex) {
        std::cerr << "[GoogleCalendar] fetch_upcoming_events error: " << ex.what() << "\n";
    }
    return result;
}

void GoogleCalendarClient::delete_event(const std::string& gcal_event_id) {
    if (!configured_ || gcal_event_id.empty()) return;
    try {
        gcal_api_request("DELETE",
            "/calendars/" + url_encode(calendar_id_) + "/events/" + url_encode(gcal_event_id));
    } catch (const std::exception& ex) {
        std::cerr << "[GoogleCalendar] delete_event failed: " << ex.what() << "\n";
    }
}
