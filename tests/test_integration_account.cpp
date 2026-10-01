// My Account: notification preferences, data export, self-delete.
#include "integration_test_base.hpp"
#include "repositories/NotificationPrefs.hpp"
#include "repositories/DuesRepository.hpp"
#include "repositories/ShiftRepository.hpp"
#include "repositories/RsvpRepository.hpp"

namespace {
const std::string kPng(
    "\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\0\x01\0\0\0\x01\x08\x06\0\0\0\x1f\x15\xc4\x89"
    "\0\0\0\rIDATx\x9cc\xf8\x0f\0\0\x01\x01\0\x05\x18\xd8N\0\0\0\0IEND\xae\x42\x60\x82", 67);
}

TEST_F(IntegrationTest, AccountPageRequiresLogin) {
    EXPECT_NE(GET("/account").code, 200);
    auto r = GET("/account", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "My Account");
    expect_contains(r, "Event reminders");
    expect_contains(r, "Download my data");
}

TEST_F(IntegrationTest, NotificationPrefsSaveAndApply) {
    NotificationPrefs prefs(*db);
    EXPECT_TRUE(prefs.wants(regular_member_id, "dues_reminder"));
    // Only event_reminder ticked -> the rest are turned off; unknown keys ignored
    auto r = POST("/account/notifications", "event_reminder=1&bogus=1", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Saved.");
    EXPECT_TRUE(prefs.wants(regular_member_id, "event_reminder"));
    EXPECT_FALSE(prefs.wants(regular_member_id, "dues_reminder"));
    EXPECT_FALSE(prefs.wants(regular_member_id, "shift_reminder"));
    EXPECT_FALSE(prefs.wants(regular_member_id, "waitlist"));
    EXPECT_TRUE(prefs.optouts(regular_member_id).count("bogus") == 0);
    // Other members unaffected
    EXPECT_TRUE(prefs.wants(admin_member_id, "dues_reminder"));

    // Dues reminders skip opted-out members
    for (int64_t id : {regular_member_id, admin_member_id}) {
        auto u = db->prepare("UPDATE members SET is_paid=1, paid_until='2099-01-10' WHERE id=?");
        u.bind(1, id); u.step();
    }
    DuesRepository dues(*db);
    auto due = dues.needing_reminder("2099-01-01", "2099-01-31");
    bool has_reg = false, has_admin = false;
    for (const auto& d : due) { has_reg |= d.member_id == regular_member_id; has_admin |= d.member_id == admin_member_id; }
    EXPECT_FALSE(has_reg);
    EXPECT_TRUE(has_admin);

    // Shift reminders too
    LugEvent e;
    e.title = "Pref Show"; e.start_time = "2099-07-01T09:00:00"; e.end_time = "2099-07-01T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    ShiftRepository shifts(*db);
    int64_t sid = 0;
    {
        auto s = db->prepare("INSERT INTO event_shifts (event_id, title, starts_at, ends_at, slots) VALUES (?, 'Setup', '2099-07-01T08:00', '2099-07-01T09:00', 5) RETURNING id");
        s.bind(1, ev.id);
        ASSERT_TRUE(s.step());
        sid = s.col_int(0);
    }
    ASSERT_TRUE(shifts.sign_up(sid, regular_member_id));
    ASSERT_TRUE(shifts.sign_up(sid, admin_member_id));
    auto dr = shifts.due_reminders("2099-06-30T00:00", "2099-07-02T00:00");
    ASSERT_EQ(dr.size(), 1u);
    EXPECT_EQ(dr[0].member_id, admin_member_id);

    // Ticking everything back on clears the opt-outs
    POST("/account/notifications", "event_reminder=1&shift_reminder=1&waitlist=1&dues_reminder=1&email=1", member_token);
    EXPECT_TRUE(prefs.optouts(regular_member_id).empty());
}

TEST_F(IntegrationTest, WaitlistPromotionStillWorksWithDm) {
    LugEvent e;
    e.title = "Tiny Show"; e.start_time = "2099-08-01T09:00:00"; e.end_time = "2099-08-01T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    e.max_attendees = 1;
    auto ev = event_svc->create(e);
    std::string url = "/events/" + std::to_string(ev.id) + "/rsvp";
    EXPECT_EQ(POST(url, "", admin_token).code, 200);
    expect_contains(POST(url, "", member_token), "waitlist");
    // Admin cancels -> member promoted (DM is queued on the Discord pool; must not block or fail)
    EXPECT_EQ(POST(url, "", admin_token).code, 200);
    RsvpRepository rsvps(*db);
    auto st = rsvps.status_of(ev.id, regular_member_id);
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, "going");
}

TEST_F(IntegrationTest, AccountExportHasOwnDataOnly) {
    POST("/account/notifications", "event_reminder=1", member_token);
    auto r = GET("/account/export", member_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.headers.find("attachment"), std::string::npos);
    auto j = crow::json::load(r.body);
    ASSERT_TRUE(j);
    EXPECT_EQ(j["profile"][0]["id"].i(), regular_member_id);
    EXPECT_EQ(j["profile"][0]["first_name"].s(), "Regular");
    EXPECT_FALSE(j["profile"][0].has("calendar_token_hash"));
    ASSERT_GE(j["sessions"].size(), 1u);
    EXPECT_FALSE(j["sessions"][0].has("token"));
    EXPECT_EQ(j["notification_optouts"].size(), 4u);
    expect_not_contains(r, "Admin U.");
    expect_not_contains(r, member_token);
}

TEST_F(IntegrationTest, AccountDelete) {
    // Needs the confirmation word
    EXPECT_EQ(POST("/account/delete", "confirm=delete", member_token).code, 400);
    // The only admin can't delete themselves
    EXPECT_EQ(POST("/account/delete", "confirm=DELETE", admin_token).code, 409);

    LugEvent e;
    e.title = "Bye Show"; e.start_time = "2099-07-01T09:00:00"; e.end_time = "2099-07-01T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    auto up = POST_FILE("/events/" + std::to_string(ev.id) + "/photos", "photo", "a.png", kPng, member_token);
    ASSERT_EQ(up.code, 200);
    auto p = up.body.find("/uploads/");
    ASSERT_NE(p, std::string::npos);
    std::string img = up.body.substr(p, up.body.find('"', p) - p);
    EXPECT_EQ(GET(img, admin_token).code, 200);

    auto r = POST("/account/delete", "confirm=DELETE", member_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.headers.find("/login"), std::string::npos);
    EXPECT_FALSE(member_repo->find_by_id(regular_member_id).has_value());
    EXPECT_NE(GET("/account", member_token).code, 200);   // session gone
    EXPECT_EQ(GET(img, admin_token).code, 404);           // photo file gone
    auto a = db->prepare("SELECT COUNT(*) FROM audit_log WHERE action='member.self_delete'");
    ASSERT_TRUE(a.step());
    EXPECT_EQ(a.col_int(0), 1);
}
