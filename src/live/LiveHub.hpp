#pragma once
// Live updates: tells open pages what just changed, so they can reload the
// parts that show it (static/live.js does that side).
//
// A change is a topic, "<type>:<id>" - the audit log's entity type and id,
// e.g. "event:12" - or just "<type>" when there's no id. Only topics go out,
// never data: a page re-fetches through its normal signed-in routes, so it
// can't learn anything its viewer couldn't already open.
//
// Changes are batched (~150 ms) and each browser tab is skipped for changes it
// made itself (its htmx requests carry X-Live-Tab), since it already shows them.
#include <crow/http_request.h>
#include <crow/websocket.h>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace live {

// Who is on the other end of a connection.
struct Viewer {
    int64_t     member_id = 0;
    bool        staff = false;      // admin, moderator, chapter lead or treasurer
    std::string tab;                // the tab's id (see X-Live-Tab)
    std::string session;            // their session token, re-checked periodically
};

class Hub {
public:
    static Hub& get();

    // Something changed. `origin_tab` is the tab that made the change, if any.
    void changed(const std::string& type, int64_t id = 0, const std::string& origin_tab = "");

    // Connections. add() refuses past the limits (returns false).
    bool add(crow::websocket::connection* conn, Viewer viewer);
    void remove(crow::websocket::connection* conn);
    size_t connections();

    // Closes connections whose session has ended (signed out, expired,
    // revoked). Checked every heartbeat.
    void set_session_check(std::function<bool(const std::string& session)> check);

    // Tests: sees every batch, before per-viewer filtering.
    void set_listener(std::function<void(const std::vector<std::string>&)> listener);

    // A tab id is 8-40 letters, digits, '-' or '_' (anything else is ignored).
    static bool valid_tab(const std::string& tab);

    static constexpr size_t kMaxConnections = 1000;
    static constexpr size_t kMaxPerMember   = 20;

private:
    Hub() = default;
    void start_locked();
    void run();
    void send_batch(const std::vector<std::pair<std::string, std::string>>& batch);
    void heartbeat();

    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<std::pair<std::string, std::string>> pending_;   // topic, origin tab
    std::map<crow::websocket::connection*, Viewer> conns_;
    std::function<bool(const std::string&)> session_check_;
    std::function<void(const std::vector<std::string>&)> listener_;
    bool started_ = false;
};

inline void changed(const std::string& type, int64_t id = 0, const std::string& origin_tab = "") {
    Hub::get().changed(type, id, origin_tab);
}

// For changes made by a request that isn't audit-logged (audit.log announces
// its own): the requesting tab is skipped.
inline void changed_by(const crow::request& req, const std::string& type, int64_t id = 0) {
    changed(type, id, req.get_header_value("X-Live-Tab"));
}

} // namespace live
