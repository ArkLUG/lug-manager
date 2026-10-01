// Dashboard: "Coming up for you" and the admins' "Needs attention".
#include "integration_test_base.hpp"
#include "utils/LocalTime.hpp"

namespace {
void exec(SqliteDatabase& db, const std::string& sql) { auto st = db.prepare(sql); st.step(); }
std::string day(int offset) {
    std::time_t t = std::time(nullptr) + offset * 86400;
    std::tm tm = local_tm(t);
    char b[16];
    std::strftime(b, sizeof(b), "%Y-%m-%d", &tm);
    return b;
}
}

TEST_F(IntegrationTest, DashboardComingUpAndNeedsAttention) {
    // LUG-wide event in 5 days (the member is going), a meeting of a chapter they're not in, a past public event with no visitors
    exec(*db, "INSERT INTO lug_events (title, start_time, end_time, status, ical_uid, scope) VALUES ('Brick Fest', '" + day(5) +
              "T09:00:00', '" + day(5) + "T17:00:00', 'confirmed', 'd-1', 'lug_wide')");
    int64_t ev = db->last_insert_rowid();
    exec(*db, "INSERT INTO event_rsvps (event_id, member_id, status) VALUES (" + std::to_string(ev) + ", " + std::to_string(regular_member_id) + ", 'going')");
    exec(*db, "INSERT INTO meetings (title, start_time, end_time, ical_uid, scope, chapter_id) VALUES ('Other chapter night', '" + day(3) +
              "T19:00:00', '" + day(3) + "T21:00:00', 'd-2', 'chapter', " + std::to_string(test_chapter_id) + ")");
    exec(*db, "INSERT INTO lug_events (title, start_time, end_time, status, ical_uid, scope) VALUES ('Old show', '" + day(-10) +
              "T09:00:00', '" + day(-10) + "T17:00:00', 'confirmed', 'd-3', 'lug_wide')");
    exec(*db, "INSERT INTO event_display_requests (event_id, member_id, title) VALUES (" + std::to_string(ev) + ", " +
              std::to_string(regular_member_id) + ", 'Castle')");

    auto m = GET("/dashboard", member_token);
    expect_contains(m, "Coming up for you");
    expect_contains(m, "Brick Fest");
    expect_contains(m, "you're going");
    expect_not_contains(m, "Other chapter night");             // not their chapter
    expect_not_contains(m, "Needs attention");                 // admins only

    auto a = GET("/dashboard", admin_token);
    expect_contains(a, "Needs attention");
    expect_contains(a, "display request(s) waiting for an answer");
    expect_contains(a, "recent public event(s) without visitor numbers");
    expect_contains(a, "+ New meeting");
}
