#include "live/LiveHub.hpp"
#include <crow/json.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>

namespace live {

namespace {
// Changes plain members never see on any page.
bool staff_only(const std::string& topic) {
    const std::string type = topic.substr(0, topic.find(':'));
    return type == "treasury" || type == "api_key" || type == "audit";
}
constexpr auto kBatchDelay = std::chrono::milliseconds(150);
constexpr auto kHeartbeat  = std::chrono::seconds(25);   // under common proxy idle timeouts (60 s)
constexpr size_t kMaxTopics = 200;                        // more than this in one batch: "reload everything"
}

Hub& Hub::get() {
    static Hub* hub = new Hub();   // never destroyed: the worker thread outlives main()
    return *hub;
}

bool Hub::valid_tab(const std::string& tab) {
    if (tab.size() < 8 || tab.size() > 40) return false;
    return std::all_of(tab.begin(), tab.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

void Hub::changed(const std::string& type, int64_t id, const std::string& origin_tab) {
    if (type.empty()) return;
    std::string topic = id > 0 ? type + ":" + std::to_string(id) : type;
    std::lock_guard<std::mutex> l(mu_);
    if (conns_.empty() && !listener_) return;
    pending_.emplace_back(std::move(topic), valid_tab(origin_tab) ? origin_tab : "");
    start_locked();
    cv_.notify_one();
}

bool Hub::add(crow::websocket::connection* conn, Viewer viewer) {
    std::lock_guard<std::mutex> l(mu_);
    if (conns_.size() >= kMaxConnections) return false;
    size_t mine = std::count_if(conns_.begin(), conns_.end(),
                                [&](const auto& c) { return c.second.member_id == viewer.member_id; });
    if (mine >= kMaxPerMember) return false;
    conns_[conn] = std::move(viewer);
    start_locked();
    return true;
}

void Hub::remove(crow::websocket::connection* conn) {
    std::lock_guard<std::mutex> l(mu_);
    conns_.erase(conn);
}

void Hub::clear() {
    std::lock_guard<std::mutex> l(mu_);
    conns_.clear();
    pending_.clear();
}

size_t Hub::connections() {
    std::lock_guard<std::mutex> l(mu_);
    return conns_.size();
}

void Hub::set_session_check(std::function<bool(const std::string&)> check) {
    std::lock_guard<std::mutex> l(mu_);
    session_check_ = std::move(check);
}

void Hub::set_listener(std::function<void(const std::vector<std::string>&)> listener) {
    std::lock_guard<std::mutex> l(mu_);
    listener_ = std::move(listener);
    start_locked();
}

void Hub::start_locked() {
    if (started_) return;
    started_ = true;
    std::thread([this] { run(); }).detach();
}

void Hub::run() {
    for (;;) {
        std::vector<std::pair<std::string, std::string>> batch;
        {
            std::unique_lock<std::mutex> l(mu_);
            if (!cv_.wait_for(l, kHeartbeat, [this] { return !pending_.empty(); })) {
                l.unlock();
                heartbeat();
                continue;
            }
        }
        std::this_thread::sleep_for(kBatchDelay);   // gather a burst (bulk edits) into one message
        {
            std::lock_guard<std::mutex> l(mu_);
            batch.swap(pending_);
        }
        send_batch(batch);
    }
}

void Hub::send_batch(const std::vector<std::pair<std::string, std::string>>& batch) {
    std::set<std::string> all;
    for (const auto& [topic, origin] : batch) all.insert(topic);
    std::lock_guard<std::mutex> l(mu_);
    if (listener_) listener_(std::vector<std::string>(all.begin(), all.end()));
    for (auto& [conn, viewer] : conns_) {
        std::set<std::string> topics;
        for (const auto& [topic, origin] : batch) {
            if (!origin.empty() && origin == viewer.tab) continue;   // they made it; they already see it
            if (!viewer.staff && staff_only(topic)) continue;
            topics.insert(topic);
        }
        if (topics.empty()) continue;
        crow::json::wvalue msg;
        if (topics.size() > kMaxTopics) {
            msg["all"] = true;
        } else {
            msg["changes"] = crow::json::wvalue::list();
            size_t i = 0;
            for (const auto& t : topics) msg["changes"][i++] = t;
        }
        conn->send_text(msg.dump());
    }
}

void Hub::heartbeat() {
    std::function<bool(const std::string&)> check;
    std::vector<std::pair<crow::websocket::connection*, std::string>> sessions;
    {
        std::lock_guard<std::mutex> l(mu_);
        check = session_check_;
        for (const auto& [conn, viewer] : conns_) sessions.emplace_back(conn, viewer.session);
    }
    std::map<crow::websocket::connection*, std::string> ended;   // checked outside the lock (it reads the DB)
    if (check)
        for (const auto& [conn, session] : sessions)
            if (!check(session)) ended[conn] = session;
    std::lock_guard<std::mutex> l(mu_);
    for (auto it = conns_.begin(); it != conns_.end();) {
        auto e = ended.find(it->first);
        if (e != ended.end() && e->second == it->second.session) {
            it->first->close("signed out");
            it = conns_.erase(it);
        } else {
            it->first->send_text("{}");   // keeps proxies from dropping an idle connection
            ++it;
        }
    }
}

} // namespace live
