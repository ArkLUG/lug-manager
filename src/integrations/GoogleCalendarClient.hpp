#pragma once
#include "models/Meeting.hpp"
#include "models/LugEvent.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <ctime>

struct GCalImportedEvent {
    std::string google_id;
    std::string title;
    std::string description;
    std::string location;
    std::string start_time;  // ISO 8601
    std::string end_time;
    bool        is_all_day = false;
};

class GoogleCalendarClient {
public:
    GoogleCalendarClient();

    void reconfigure(const std::string& service_account_json_path,
                     const std::string& calendar_id,
                     const std::string& timezone = "");
    bool is_configured() const;

    // CRUD — return Google Calendar event ID on create
    std::string create_event(const Meeting& m);
    std::string create_event(const LugEvent& e);
    void        update_event(const std::string& gcal_event_id, const Meeting& m);
    void        update_event(const std::string& gcal_event_id, const LugEvent& e);
    void        delete_event(const std::string& gcal_event_id);

    // Fetch upcoming events from Google Calendar (for import)
    std::vector<GCalImportedEvent> fetch_upcoming_events(int max_results = 50);

private:
    std::string service_account_json_path_;
    std::string calendar_id_;
    std::string timezone_;

    // Service account fields
    std::string sa_client_email_;
    std::string sa_private_key_;
    std::string sa_token_uri_;
    bool        configured_ = false;

    // Cached OAuth2 token
    mutable std::mutex  token_mutex_;
    mutable std::string access_token_;
    mutable time_t      token_expiry_ = 0;

    void        load_service_account();
    std::string ensure_access_token() const;
    std::string build_jwt() const;
    std::string sign_jwt(const std::string& header_payload) const;
    std::string gcal_api_request(const std::string& method,
                                  const std::string& endpoint,
                                  const std::string& json_body = "") const;
    std::string build_event_json(const std::string& title,
                                  const std::string& description,
                                  const std::string& location,
                                  const std::string& start_time,
                                  const std::string& end_time,
                                  const std::string& status = "",
                                  bool all_day = false) const;

    static size_t write_cb(void* contents, size_t size, size_t nmemb, std::string* s);
    static std::string base64url_encode(const unsigned char* data, size_t len);
    static std::string url_encode(const std::string& s);
};
