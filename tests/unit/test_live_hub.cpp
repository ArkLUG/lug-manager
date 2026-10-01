// live::Hub: batching, skipping the tab that made a change, staff-only topics,
// connection limits. Uses stand-in websocket connections.
#include <gtest/gtest.h>
#include "live/LiveHub.hpp"
#include <chrono>
#include <mutex>
#include <thread>

namespace {

struct FakeConn : crow::websocket::connection {
    std::mutex mu;
    std::vector<std::string> sent;
    bool closed = false;
    void send_binary(std::string) override {}
    void send_text(std::string msg) override { std::lock_guard<std::mutex> l(mu); if (msg != "{}") sent.push_back(msg); }
    void send_ping(std::string) override {}
    void send_pong(std::string) override {}
    void close(std::string const&) override { closed = true; }
    std::string get_remote_ip() override { return "127.0.0.1"; }
    std::vector<std::string> take() { std::lock_guard<std::mutex> l(mu); auto v = sent; sent.clear(); return v; }
};

live::Viewer viewer(int64_t member, bool staff, const std::string& tab) {
    live::Viewer v;
    v.member_id = member; v.staff = staff; v.tab = tab; v.session = "s" + std::to_string(member);
    return v;
}
void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(450)); }

} // namespace

TEST(LiveHub, TabIds) {
    EXPECT_TRUE(live::Hub::valid_tab("abcd1234"));
    EXPECT_TRUE(live::Hub::valid_tab("k3j2-x_9zzzzzzz"));
    EXPECT_FALSE(live::Hub::valid_tab("short"));
    EXPECT_FALSE(live::Hub::valid_tab("has space 12345"));
    EXPECT_FALSE(live::Hub::valid_tab("quote\"12345678"));
}

TEST(LiveHub, BatchesAndSkipsTheTabThatMadeTheChange) {
    auto& hub = live::Hub::get();
    FakeConn a, b;
    ASSERT_TRUE(hub.add(&a, viewer(1, false, "tab-aaaaaaaa")));
    ASSERT_TRUE(hub.add(&b, viewer(2, false, "tab-bbbbbbbb")));
    hub.changed("event", 12, "tab-aaaaaaaa");
    hub.changed("member", 3);
    hub.changed("event", 12);       // duplicates collapse
    settle();
    auto ma = a.take(), mb = b.take();
    ASSERT_EQ(mb.size(), 1u);                                    // one message for the burst
    EXPECT_EQ(mb[0], R"({"changes":["event:12","member:3"]})");
    ASSERT_EQ(ma.size(), 1u);
    EXPECT_NE(ma[0].find("member:3"), std::string::npos);
    EXPECT_NE(ma[0].find("event:12"), std::string::npos);        // also changed by someone else
    hub.changed("event", 7, "tab-aaaaaaaa");
    settle();
    EXPECT_TRUE(a.take().empty());                               // only their own change: nothing
    EXPECT_EQ(b.take().size(), 1u);
    hub.remove(&a);
    hub.remove(&b);
    hub.changed("event", 1);
    settle();
    EXPECT_TRUE(a.take().empty());
}

TEST(LiveHub, StaffOnlyTopics) {
    auto& hub = live::Hub::get();
    FakeConn member, staff;
    hub.add(&member, viewer(11, false, ""));
    hub.add(&staff, viewer(12, true, ""));
    hub.changed("treasury", 4);
    hub.changed("audit");
    settle();
    EXPECT_TRUE(member.take().empty());
    auto s = staff.take();
    ASSERT_EQ(s.size(), 1u);
    EXPECT_EQ(s[0], R"({"changes":["audit","treasury:4"]})");
    hub.remove(&member);
    hub.remove(&staff);
}

TEST(LiveHub, ConnectionLimitPerMember) {
    auto& hub = live::Hub::get();
    std::vector<std::unique_ptr<FakeConn>> conns;
    size_t accepted = 0;
    for (size_t i = 0; i < live::Hub::kMaxPerMember + 3; ++i) {
        conns.push_back(std::make_unique<FakeConn>());
        accepted += hub.add(conns.back().get(), viewer(99, false, "")) ? 1 : 0;
    }
    EXPECT_EQ(accepted, live::Hub::kMaxPerMember);
    EXPECT_TRUE(hub.add(conns.back().get(), viewer(98, false, "")));   // someone else still can
    for (auto& c : conns) hub.remove(c.get());
}
