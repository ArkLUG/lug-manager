// The /live websocket end to end: who may connect, and that a change made in
// one place reaches another signed-in page (and not the tab that made it).
#include "integration_test_base.hpp"
#include <curl/curl.h>
#include <chrono>
#include <thread>

namespace {

struct Ws {
    CURL* curl = nullptr;
    bool ok = false;
    ~Ws() { if (curl) curl_easy_cleanup(curl); }
};

std::unique_ptr<Ws> open_ws(int port, const std::string& session, const std::string& tab,
                            const std::string& origin = "") {
    auto ws = std::make_unique<Ws>();
    ws->curl = curl_easy_init();
    std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/live?tab=" + tab;
    curl_easy_setopt(ws->curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(ws->curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(ws->curl, CURLOPT_TIMEOUT, 5L);
    struct curl_slist* h = nullptr;
    if (!session.empty()) h = curl_slist_append(h, ("Cookie: session=" + session).c_str());
    if (!origin.empty()) h = curl_slist_append(h, ("Origin: " + origin).c_str());
    if (h) curl_easy_setopt(ws->curl, CURLOPT_HTTPHEADER, h);
    long code = 0;
    ws->ok = curl_easy_perform(ws->curl) == CURLE_OK &&
             curl_easy_getinfo(ws->curl, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK && code == 101;
    if (h) curl_slist_free_all(h);
    return ws;
}

// Text frames received within `ms`, heartbeats ("{}") left out.
std::string receive(Ws& ws, int ms = 1500) {
    std::string out;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    char buf[4096];
    while (std::chrono::steady_clock::now() < until) {
        size_t n = 0;
        const struct curl_ws_frame* meta = nullptr;
        CURLcode rc = curl_ws_recv(ws.curl, buf, sizeof(buf), &n, &meta);
        if (rc == CURLE_AGAIN) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
        if (rc != CURLE_OK) break;
        std::string msg(buf, n);
        if (msg != "{}") out += msg + "\n";
    }
    return out;
}

} // namespace

TEST_F(IntegrationTest, LiveRequiresSignInAndSameOrigin) {
    EXPECT_FALSE(open_ws(port, "", "tab-anon0001")->ok);
    EXPECT_FALSE(open_ws(port, "not-a-session", "tab-anon0002")->ok);
    EXPECT_FALSE(open_ws(port, member_token, "tab-evil0001", "https://evil.example")->ok);
    EXPECT_TRUE(open_ws(port, member_token, "tab-good0001", "http://127.0.0.1:" + std::to_string(port))->ok);
    EXPECT_TRUE(open_ws(port, member_token, "tab-good0002")->ok);
}

TEST_F(IntegrationTest, LiveChangesReachOtherPagesNotTheSender) {
    auto watcher = open_ws(port, member_token, "tab-watch001");
    auto editor  = open_ws(port, admin_token, "tab-edit0001");
    ASSERT_TRUE(watcher->ok);
    ASSERT_TRUE(editor->ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));   // let both register

    // An admin edits a member from the "tab-edit0001" tab.
    auto r = http("POST", "/members/" + std::to_string(regular_member_id),
                  "first_name=Live&last_name=Update", admin_token, true, "", false, {"X-Live-Tab: tab-edit0001"});
    ASSERT_EQ(r.code, 200);
    std::string seen = receive(*watcher);
    EXPECT_NE(seen.find("\"member:" + std::to_string(regular_member_id) + "\""), std::string::npos) << seen;
    EXPECT_EQ(seen.find("\"audit\""), std::string::npos) << seen;            // staff-only topic
    std::string own = receive(*editor, 500);
    EXPECT_EQ(own.find("member:"), std::string::npos) << own;                // the editing tab already shows it
}
