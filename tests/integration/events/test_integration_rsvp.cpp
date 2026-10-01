// Event RSVPs: capacity, waitlist promotion, deadline.
#include "integration_test_base.hpp"

namespace {
LugEvent future_event(const std::string& title, int max_attendees) {
    LugEvent e;
    e.title = title;
    e.start_time = "2099-05-01T10:00:00";
    e.end_time = "2099-05-01T16:00:00";
    e.scope = "lug_wide";
    e.status = "confirmed";
    e.max_attendees = max_attendees;
    e.suppress_discord = true;
    e.suppress_calendar = true;
    return e;
}
}

TEST_F(IntegrationTest, RsvpCapacityAndWaitlistPromotion) {
    auto ev = event_svc->create(future_event("Capacity Event", 1));
    std::string url = "/events/" + std::to_string(ev.id) + "/rsvp";

    auto a = POST(url, "", member_token);
    EXPECT_EQ(a.code, 200);
    expect_contains(a, "You're going");

    auto b = POST(url, "", chapter_lead_token);
    EXPECT_EQ(b.code, 200);
    expect_contains(b, "waitlist");

    // First member cancels -> waitlisted member is promoted
    auto c = POST(url, "", member_token);
    EXPECT_EQ(c.code, 200);
    auto panel = GET(url, chapter_lead_token);
    expect_contains(panel, "You're going");
}

TEST_F(IntegrationTest, RsvpClosedAfterDeadline) {
    auto e = future_event("Deadline Event", 0);
    e.signup_deadline = "2020-01-01";
    auto ev = event_svc->create(e);
    auto r = POST("/events/" + std::to_string(ev.id) + "/rsvp", "", member_token);
    EXPECT_EQ(r.code, 409);
    expect_contains(r, "closed");
}

TEST_F(IntegrationTest, RsvpRemoveRequiresManager) {
    auto ev = event_svc->create(future_event("Remove Event", 0));
    POST("/events/" + std::to_string(ev.id) + "/rsvp", "", chapter_lead_token);
    auto r = POST("/events/" + std::to_string(ev.id) + "/rsvp/" +
                  std::to_string(chapter_lead_member_id) + "/remove", "", member_token);
    EXPECT_EQ(r.code, 403);
    auto ok = POST("/events/" + std::to_string(ev.id) + "/rsvp/" +
                   std::to_string(chapter_lead_member_id) + "/remove", "", admin_token);
    EXPECT_EQ(ok.code, 200);
}
