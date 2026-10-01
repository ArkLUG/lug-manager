// Volunteer shifts.
#include "integration_test_base.hpp"

namespace {
LugEvent shifts_event() {
    LugEvent e;
    e.title = "Shift Show";
    e.start_time = "2099-07-01T09:00:00";
    e.end_time = "2099-07-01T17:00:00";
    e.scope = "lug_wide";
    e.status = "confirmed";
    e.suppress_discord = true;
    e.suppress_calendar = true;
    return e;
}
}

TEST_F(IntegrationTest, ShiftsCreateSignupCapacity) {
    auto ev = event_svc->create(shifts_event());
    std::string base = "/events/" + std::to_string(ev.id) + "/shifts";
    EXPECT_EQ(POST(base, "title=Setup&day=2099-07-01&from=08:00&to=09:00&slots=1", member_token).code, 403);
    EXPECT_EQ(POST(base, "title=Setup&day=2099-07-01&from=09:00&to=08:00&slots=1", admin_token).code, 400);
    auto c = POST(base, "title=Setup&day=2099-07-01&from=08:00&to=09:00&slots=1", admin_token);
    EXPECT_EQ(c.code, 200);
    expect_contains(c, "Setup");
    auto q = db->prepare("SELECT id FROM event_shifts WHERE event_id=?");
    q.bind(1, ev.id);
    ASSERT_TRUE(q.step());
    std::string sid = std::to_string(q.col_int(0));

    auto mine = POST(base + "/" + sid + "/signup", "", member_token);
    EXPECT_EQ(mine.code, 200);
    expect_contains(mine, "Thanks for volunteering");
    auto full = POST(base + "/" + sid + "/signup", "", chapter_lead_token);
    EXPECT_EQ(full.code, 409);
    // Manager sees who signed up; members don't
    expect_contains(GET(base, admin_token), "Regular U.");
    EXPECT_EQ(GET(base, chapter_lead_token).body.find("Regular U."), std::string::npos);
    // Toggle off frees the slot
    POST(base + "/" + sid + "/signup", "", member_token);
    EXPECT_EQ(POST(base + "/" + sid + "/signup", "", chapter_lead_token).code, 200);
    EXPECT_EQ(POST(base + "/" + sid + "/delete", "", member_token).code, 403);
    EXPECT_EQ(POST(base + "/" + sid + "/delete", "", admin_token).code, 200);
}
