// The /live websocket end to end: who may connect, and that a change made in
// one place reaches another signed-in page (and not the tab that made it).
#include "integration_test_base.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <thread>

namespace {

// A minimal websocket client on a plain socket (libcurl's websocket support
// isn't in every distribution's build): the upgrade handshake, then reading
// the server's unmasked text frames.
struct Ws {
    int fd = -1;
    bool ok = false;
    std::string buf;
    ~Ws() { if (fd >= 0) close(fd); }
};

std::unique_ptr<Ws> open_ws(int port, const std::string& session, const std::string& tab,
                            const std::string& origin = "") {
    auto ws = std::make_unique<Ws>();
    ws->fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(ws->fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return ws;
    std::string req = "GET /live?tab=" + tab + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
    if (!session.empty()) req += "Cookie: session=" + session + "\r\n";
    if (!origin.empty()) req += "Origin: " + origin + "\r\n";
    req += "\r\n";
    if (send(ws->fd, req.data(), req.size(), 0) != static_cast<ssize_t>(req.size())) return ws;
    timeval tv{2, 0};
    setsockopt(ws->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char b[4096];
    while (ws->buf.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(ws->fd, b, sizeof(b), 0);
        if (n <= 0) return ws;   // refused: the server just closes
        ws->buf.append(b, static_cast<size_t>(n));
    }
    ws->ok = ws->buf.rfind("HTTP/1.1 101", 0) == 0;
    ws->buf.erase(0, ws->buf.find("\r\n\r\n") + 4);
    return ws;
}

// Text frames received within `ms`, heartbeats ("{}") left out.
std::string receive(Ws& ws, int ms = 1500) {
    std::string out;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    timeval tv{0, 100 * 1000};
    setsockopt(ws.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char b[4096];
    while (std::chrono::steady_clock::now() < until) {
        ssize_t n = recv(ws.fd, b, sizeof(b), 0);
        if (n > 0) ws.buf.append(b, static_cast<size_t>(n));
        else if (n == 0) break;
        // Parse whole frames: FIN+opcode, length (7-bit or 16-bit), payload.
        while (ws.buf.size() >= 2) {
            size_t len = static_cast<unsigned char>(ws.buf[1]) & 0x7f, head = 2;
            if (len == 126) {
                if (ws.buf.size() < 4) break;
                len = (static_cast<unsigned char>(ws.buf[2]) << 8) | static_cast<unsigned char>(ws.buf[3]);
                head = 4;
            }
            if (ws.buf.size() < head + len) break;
            std::string msg = ws.buf.substr(head, len);
            if ((ws.buf[0] & 0x0f) == 1 && msg != "{}") out += msg + "\n";
            ws.buf.erase(0, head + len);
        }
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
