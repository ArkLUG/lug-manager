// Display (MOC) space requests for events.
#include "integration_test_base.hpp"

namespace {
LugEvent show_event() {
    LugEvent e;
    e.title = "Public Show";
    e.start_time = "2099-06-01T09:00:00";
    e.end_time = "2099-06-02T17:00:00";
    e.scope = "lug_wide";
    e.status = "confirmed";
    e.suppress_discord = true;
    e.suppress_calendar = true;
    return e;
}
}

TEST_F(IntegrationTest, DisplayRequestsFlow) {
    auto ev = event_svc->create(show_event());
    std::string base = "/events/" + std::to_string(ev.id) + "/displays";

    // Closed by default
    auto closed = POST(base, "title=Town&width_in=48&depth_in=30", member_token);
    EXPECT_EQ(closed.code, 409);

    // Only managers can open requests
    EXPECT_EQ(POST(base + "/toggle", "", member_token).code, 403);
    EXPECT_EQ(POST(base + "/toggle", "", admin_token).code, 200);

    auto bad = POST(base, "title=&width_in=48&depth_in=30", member_token);
    EXPECT_EQ(bad.code, 400);
    auto ok = POST(base, "title=Modular+Town&width_in=48&depth_in=30&needs_power=on", member_token);
    EXPECT_EQ(ok.code, 200);
    expect_contains(ok, "Modular Town");
    expect_contains(ok, "pending");

    // Members only see their own requests; the manager sees everyone's
    auto other = GET(base, chapter_lead_token);
    EXPECT_EQ(other.body.find("Modular Town"), std::string::npos);

    auto mine = GET(base, admin_token);
    expect_contains(mine, "Modular Town");

    auto csv = GET(base + ".csv", admin_token);
    EXPECT_EQ(csv.code, 200);
    expect_contains(csv, "Modular Town");
    EXPECT_EQ(GET(base + ".csv", member_token).code, 403);
}

TEST_F(IntegrationTest, DisplayCsvNeutralizesFormulas) {
    auto ev = event_svc->create(show_event());
    std::string base = "/events/" + std::to_string(ev.id) + "/displays";
    POST(base + "/toggle", "", admin_token);
    POST(base, "title=%3DHYPERLINK(%22x%22)&width_in=10&depth_in=10", member_token);
    auto csv = GET(base + ".csv", admin_token);
    expect_contains(csv, "\"'=HYPERLINK");
}
