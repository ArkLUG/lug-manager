// LEGO Fan CoLab extras: ambassador history, yearly to-do, activity summary,
// and the public About page with its editor.
#include "integration_test_base.hpp"
#include "services/events/AttendanceService.hpp"
#include "services/Features.hpp"
#include "services/members/MemberMerge.hpp"
#include "utils/LocalTime.hpp"
#include <cctype>

namespace {

int64_t one_int(SqliteDatabase& db, const std::string& sql) {
    auto st = db.prepare(sql);
    return st.step() ? st.col_int(0) : -1;
}
std::string one_text(SqliteDatabase& db, const std::string& sql) {
    auto st = db.prepare(sql);
    return st.step() ? st.col_text(0) : "<none>";
}
void exec(SqliteDatabase& db, const std::string& sql) {
    auto st = db.prepare(sql);
    st.step();
}


} // namespace

TEST_F(IntegrationTest, FanCoLabPageAdminsOnlyAndFeatureToggle) {
    EXPECT_EQ(GET("/fancolab", member_token).code, 403);
    auto p = GET("/fancolab", admin_token);
    EXPECT_EQ(p.code, 200);
    expect_contains(p, "Community Ambassador history");
    expect_contains(p, "Confirm who is Community Ambassador this year");   // seeded to-do
    expect_contains(GET("/dashboard", admin_token), "hx-get=\"/fancolab\"");   // sidebar link
    expect_not_contains(GET("/dashboard", member_token), "hx-get=\"/fancolab\"");
    Features::set("fancolab", false);
    EXPECT_EQ(GET("/fancolab", admin_token).code, 404);
    EXPECT_EQ(GET("/fancolab/summary.csv", admin_token).code, 404);
    Features::set("fancolab", true);
}

TEST_F(IntegrationTest, FanCoLabAmbassadorHistory) {
    const std::string today = AttendanceService::today_ymd();
    const std::string reg = std::to_string(regular_member_id), adm = std::to_string(admin_member_id);
    EXPECT_EQ(POST("/fancolab/recognition", "fan_colab_recognized=1&community_ambassador_id=" + reg, member_token).code, 403);
    EXPECT_EQ(POST("/fancolab/recognition", "fan_colab_recognized=1&community_ambassador_id=999999", admin_token).code, 400);

    auto r = POST("/fancolab/recognition", "fan_colab_recognized=1&community_ambassador_id=" + reg, admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(settings_repo->get("community_ambassador_id", ""), reg);
    EXPECT_EQ(one_int(*db, "SELECT COUNT(*) FROM community_ambassador_terms WHERE ended_on IS NULL AND member_id=" + reg), 1);
    // Saving again without a change keeps the one open term
    POST("/fancolab/recognition", "fan_colab_recognized=1&community_ambassador_id=" + reg, admin_token);
    EXPECT_EQ(one_int(*db, "SELECT COUNT(*) FROM community_ambassador_terms"), 1);

    // Back-date the current term, then hand over: the old term ends today
    int64_t term = one_int(*db, "SELECT id FROM community_ambassador_terms");
    EXPECT_EQ(POST("/fancolab/terms/" + std::to_string(term), "started_on=2024-03-01&ended_on=2025-01-01", admin_token).code, 400); // serving: no end
    EXPECT_EQ(POST("/fancolab/terms/" + std::to_string(term), "started_on=2999-01-01", admin_token).code, 400);
    EXPECT_EQ(POST("/fancolab/terms/" + std::to_string(term), "started_on=2024-03-01", admin_token).code, 200);
    EXPECT_EQ(POST("/fancolab/terms/" + std::to_string(term) + "/delete", "", admin_token).code, 400);   // can't delete the current one
    POST("/fancolab/recognition", "fan_colab_recognized=1&community_ambassador_id=" + adm, admin_token);
    EXPECT_EQ(one_text(*db, "SELECT ended_on FROM community_ambassador_terms WHERE id=" + std::to_string(term)), today);
    EXPECT_EQ(one_int(*db, "SELECT member_id FROM community_ambassador_terms WHERE ended_on IS NULL"), admin_member_id);
    // A same-day change replaces the term started today instead of leaving a zero-length one
    POST("/fancolab/recognition", "fan_colab_recognized=1&community_ambassador_id=" + reg, admin_token);
    EXPECT_EQ(one_int(*db, "SELECT COUNT(*) FROM community_ambassador_terms WHERE member_id=" + adm), 0);
    EXPECT_EQ(one_int(*db, "SELECT COUNT(*) FROM community_ambassador_terms"), 2);

    // Earlier ambassadors
    EXPECT_EQ(POST("/fancolab/terms", "member_id=" + adm + "&started_on=2020-01-01", admin_token).code, 400);
    EXPECT_EQ(POST("/fancolab/terms", "member_id=" + adm + "&started_on=2021-01-01&ended_on=2020-01-01", admin_token).code, 400);
    EXPECT_EQ(POST("/fancolab/terms", "member_id=" + adm + "&started_on=2021-01-01&ended_on=2999-01-01", admin_token).code, 400);
    EXPECT_EQ(POST("/fancolab/terms", "member_id=999999&started_on=2020-01-01&ended_on=2021-01-01", admin_token).code, 400);
    auto add = POST("/fancolab/terms", "member_id=" + adm + "&started_on=2020-01-01&ended_on=2021-06-30", admin_token);
    EXPECT_EQ(add.code, 200);
    expect_contains(add, "2021-06-30");
    int64_t past = one_int(*db, "SELECT id FROM community_ambassador_terms WHERE started_on='2020-01-01'");
    EXPECT_EQ(POST("/fancolab/terms/" + std::to_string(past), "started_on=2020-02-01&ended_on=", admin_token).code, 400);
    EXPECT_EQ(POST("/fancolab/terms/" + std::to_string(past), "started_on=2020-02-01&ended_on=2021-06-30", admin_token).code, 200);
    EXPECT_EQ(one_text(*db, "SELECT started_on FROM community_ambassador_terms WHERE id=" + std::to_string(past)), "2020-02-01");

    // The yearly summary names everyone who served that year
    auto s = GET("/fancolab/summary?year=2021", admin_token);
    expect_contains(s, "Community Ambassador: " + one_text(*db, "SELECT display_name FROM members WHERE id=" + adm) + " (from 2020-02-01 to 2021-06-30)");
    expect_not_contains(s, "from 2024-03-01");

    // Merging members carries the history over
    Member dup; dup.first_name = "Reg"; dup.last_name = "Dup"; dup.display_name = "Reg D."; dup.role = "member";
    int64_t keep = member_repo->create(dup).id;
    MemberMerge(*db).merge(keep, regular_member_id);
    EXPECT_EQ(one_int(*db, "SELECT COUNT(*) FROM community_ambassador_terms WHERE member_id=" + std::to_string(keep)), 2);

    // Past terms can be removed; "nobody" ends the current term
    EXPECT_EQ(POST("/fancolab/terms/" + std::to_string(past) + "/delete", "", admin_token).code, 200);
    POST("/fancolab/recognition", "fan_colab_recognized=1&community_ambassador_id=", admin_token);
    EXPECT_EQ(one_int(*db, "SELECT COUNT(*) FROM community_ambassador_terms WHERE ended_on IS NULL"), 0);

    // The setup checklist's form records history too
    POST("/setup/fancolab", "fan_colab_recognized=1&community_ambassador_id=" + adm, admin_token);
    EXPECT_EQ(one_int(*db, "SELECT member_id FROM community_ambassador_terms WHERE ended_on IS NULL"), admin_member_id);
}

TEST_F(IntegrationTest, FanCoLabYearlyTodo) {
    const int year = local_tm(std::time(nullptr)).tm_year + 1900;
    const std::string ys = std::to_string(year);
    // Reminder only for recognized groups, only for admins
    expect_not_contains(GET("/dashboard", admin_token), "LEGO Fan CoLab to-do list");
    settings_repo->set("fan_colab_recognized", "1");
    expect_contains(GET("/dashboard", admin_token), ">3</span>thing(s) left on this year&#39;s LEGO Fan CoLab to-do list");
    expect_not_contains(GET("/dashboard", member_token), "LEGO Fan CoLab to-do list");

    int64_t first = one_int(*db, "SELECT id FROM fan_colab_tasks ORDER BY sort_order LIMIT 1");
    std::string toggle = "/fancolab/tasks/" + std::to_string(first) + "/toggle";
    EXPECT_EQ(POST(toggle, "year=" + ys, member_token).code, 403);
    auto t = POST(toggle, "year=" + ys, admin_token);
    EXPECT_EQ(t.code, 200);
    expect_contains(t, "1 of 3 done");
    EXPECT_EQ(one_int(*db, "SELECT done_by FROM fan_colab_task_done WHERE task_id=" + std::to_string(first)), admin_member_id);
    expect_contains(GET("/dashboard", admin_token), ">2</span>thing(s) left on this year&#39;s LEGO Fan CoLab");
    // Ticks are per year
    expect_contains(GET("/fancolab?year=" + std::to_string(year + 1), admin_token), "0 of 3 done");
    // Untick
    expect_contains(POST(toggle, "year=" + ys, admin_token), "0 of 3 done");
    POST(toggle, "year=" + ys, admin_token);

    // Edit the list
    EXPECT_EQ(POST("/fancolab/tasks", "title=+++&year=" + ys, admin_token).code, 400);
    auto a = POST("/fancolab/tasks", "title=Renew+the+venue+booking&year=" + ys, admin_token);
    expect_contains(a, "Renew the venue booking");
    expect_contains(a, "1 of 4 done");
    int64_t added = one_int(*db, "SELECT id FROM fan_colab_tasks WHERE title='Renew the venue booking'");
    EXPECT_EQ(POST("/fancolab/tasks/" + std::to_string(added) + "/delete", "year=" + ys, member_token).code, 403);
    expect_contains(POST("/fancolab/tasks/" + std::to_string(added) + "/delete", "year=" + ys, admin_token), "1 of 3 done");

    // All done: the reminder goes away
    auto st = db->prepare("SELECT id FROM fan_colab_tasks WHERE id <> ?");
    st.bind(1, first);
    std::vector<int64_t> rest;
    while (st.step()) rest.push_back(st.col_int(0));
    for (auto id : rest) POST("/fancolab/tasks/" + std::to_string(id) + "/toggle", "year=" + ys, admin_token);
    expect_contains(GET("/fancolab", admin_token), "3 of 3 done");
    expect_not_contains(GET("/dashboard", admin_token), "LEGO Fan CoLab to-do list");
    // Switched off: no reminder
    exec(*db, "DELETE FROM fan_colab_task_done");
    Features::set("fancolab", false);
    expect_not_contains(GET("/dashboard", admin_token), "LEGO Fan CoLab to-do list");
    Features::set("fancolab", true);
}

TEST_F(IntegrationTest, FanCoLabActivitySummary) {
    settings_repo->set("lug_name", "Test LUG");
    settings_repo->set("fan_colab_recognized", "1");
    auto make = [&](const std::string& title, const std::string& start, const std::string& end) {
        LugEvent e;
        e.title = title; e.start_time = start; e.end_time = end; e.location = "Expo Hall";
        e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
        return event_svc->create(e).id;
    };
    int64_t show = make("Brick Fest", "2024-06-01T09:00:00", "2024-06-02T17:00:00");
    int64_t priv = make("Members BBQ", "2024-07-04T12:00:00", "2024-07-04T15:00:00");
    make("Old show", "2023-06-01T09:00:00", "2023-06-01T17:00:00");
    exec(*db, "UPDATE lug_events SET public_kids=40, public_teens=10, public_adults=50, public_interest=7 WHERE id=" + std::to_string(show));
    exec(*db, "UPDATE lug_events SET is_private=1 WHERE id=" + std::to_string(priv));
    exec(*db, "INSERT INTO event_day_attendance (event_day_id, member_id) SELECT id, " + std::to_string(regular_member_id) +
              " FROM event_days WHERE event_id=" + std::to_string(show));
    exec(*db, "INSERT INTO event_display_requests (event_id, member_id, title, width_in, depth_in, status) VALUES (" +
              std::to_string(show) + "," + std::to_string(regular_member_id) + ",'Castle',48,24,'approved'),(" +
              std::to_string(show) + "," + std::to_string(regular_member_id) + ",'Train',24,24,'declined')");
    exec(*db, "INSERT INTO meetings (title, start_time, end_time, ical_uid) VALUES ('March meeting','2024-03-05T19:00:00','2024-03-05T21:00:00','fc-sum-1')");
    exec(*db, "INSERT INTO attendance (member_id, entity_type, entity_id) SELECT " + std::to_string(admin_member_id) +
              ", 'meeting', id FROM meetings WHERE ical_uid='fc-sum-1'");

    EXPECT_EQ(GET("/fancolab/summary?year=2024", member_token).code, 403);
    auto p = GET("/fancolab/summary?year=2024", admin_token);
    EXPECT_EQ(p.code, 200);
    expect_contains(p, "Test LUG: 2024 activity summary");
    expect_contains(p, "A Recognized LEGO® Fan Community");
    expect_contains(p, "Brick Fest");
    expect_not_contains(p, "Members BBQ");
    expect_not_contains(p, "Old show");

    auto csv = GET("/fancolab/summary.csv?year=2024", admin_token);
    EXPECT_EQ(csv.code, 200);
    EXPECT_NE(csv.headers.find("fan-colab-summary-2024.csv"), std::string::npos);
    expect_contains(csv, "\"Meetings held\",\"1\"");
    expect_contains(csv, "\"Meeting check-ins\",\"1\"");
    expect_contains(csv, "\"Public shows and events\",\"1\"");
    expect_contains(csv, "\"Public show days\",\"2\"");
    expect_contains(csv, "\"Public visitors\",\"100\"");
    expect_contains(csv, "\"Public visitors - kids\",\"40\"");
    expect_contains(csv, "\"Member attendances at public events\",\"1\"");
    expect_contains(csv, "\"Displays (MOCs) shown\",\"1\"");
    expect_contains(csv, "\"Display area (sq ft)\",\"8.0\"");
    expect_contains(csv, "\"Members-only events\",\"1\"");
    expect_contains(csv, "\"Members who attended something\",\"2\"");
    expect_contains(csv, "\"Recognized LEGO Fan Community\",\"Yes\"");

    auto ev = GET("/fancolab/events.csv?year=2024", admin_token);
    expect_contains(ev, "\"2024-06-01\",\"2024-06-02\",\"Brick Fest\",\"Expo Hall\",\"2\",\"100\",\"40\",\"10\",\"50\",\"1\",\"1\",\"8.0\",\"7\"");
    expect_not_contains(ev, "Members BBQ");
    EXPECT_EQ(GET("/fancolab/events.csv?year=2024", member_token).code, 403);
}

TEST_F(IntegrationTest, AboutPageEditorAndPublicPage) {
    settings_repo->set("lug_name", "Test LUG");
    EXPECT_EQ(GET("/about").code, 404);                      // off until turned on
    EXPECT_EQ(GET("/settings/about", member_token).code, 403);
    auto ed = GET("/settings/about", admin_token);
    EXPECT_EQ(ed.code, 200);
    expect_contains(ed, "data-rich-editor");
    expect_contains(ed, "/static/rich_editor.js");
    EXPECT_EQ(POST("/settings/about", "enabled=1&markdown=hi", member_token).code, 403);

    // Photo for the page: admins only, JSON url back, public once placed
    EXPECT_EQ(POST_FILE("/settings/about/photo", "photo", "a.png", kTinyPng, member_token).code, 403);
    EXPECT_EQ(POST_FILE("/settings/about/photo", "photo", "a.txt", "not an image", admin_token).code, 400);
    auto up = POST_FILE("/settings/about/photo", "photo", "a.png", kTinyPng, admin_token);
    ASSERT_EQ(up.code, 200);
    auto j = crow::json::load(up.body);
    ASSERT_TRUE(j);
    std::string url = j["url"].s();
    ASSERT_EQ(url.rfind("/about/photos/", 0), 0u);
    std::string file = url.substr(14);
    EXPECT_EQ(GET(url).code, 200);                          // no login needed
    EXPECT_EQ(GET("/about/photos/not-an-about-photo.png").code, 404);

    // Ordinary uploads stay members-only and aren't reachable through /about/photos
    exec(*db, "DELETE FROM about_photos");
    EXPECT_EQ(GET(url).code, 404);
    exec(*db, "INSERT INTO about_photos (file) VALUES ('" + file + "')");

    std::string md =
        "## Who we are\n\nWe build **LEGO** together. <script>alert(1)</script>\n\n"
        "- Meetings monthly\n- Shows\n\n[Join](https://example.org/join) [bad](javascript:alert(2))\n\n"
        "![Our table](" + url + ")\n\nTail </script><script>alert(3)</script>";
    auto encode = [](const std::string& s) {
        std::string o;
        char b[4];
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.') o += static_cast<char>(c);
            else { std::snprintf(b, sizeof(b), "%%%02X", c); o += b; }
        }
        return o;
    };
    auto save = POST("/settings/about", "enabled=1&show_numbers=1&about_title=&markdown=" + encode(md), admin_token);
    EXPECT_EQ(save.code, 200);
    expect_contains(save, "Your About page is public at");
    EXPECT_TRUE(Features::on("about_page"));

    auto a = GET("/about");
    EXPECT_EQ(a.code, 200);
    expect_contains(a, "<title>About Test LUG</title>");
    expect_contains(a, "<h2>Who we are</h2>");
    expect_contains(a, "<strong>LEGO</strong>");
    expect_contains(a, "<li>Meetings monthly</li>");
    expect_contains(a, "href=\"https://example.org/join\"");
    expect_contains(a, "src=\"" + url + "\"");
    expect_contains(a, "&lt;script&gt;alert(1)");
    expect_not_contains(a, "<script>alert");
    expect_not_contains(a, "javascript:alert");
    expect_contains(a, "<meta name=\"description\" content=\"Who we are We build LEGO together.");
    expect_contains(a, "\\u003c/script\\u003e");               // JSON-LD can't close its own tag
    expect_contains(a, "Members sign in");
    expect_not_contains(a, "The past twelve months");          // no activity yet

    // Numbers from the past year appear when there are any
    LugEvent e;
    e.title = "Recent show"; e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    e.start_time = one_text(*db, "SELECT date('now','-1 month') || 'T10:00:00'");
    e.end_time = one_text(*db, "SELECT date('now','-1 month') || 'T16:00:00'");
    int64_t id = event_svc->create(e).id;
    exec(*db, "UPDATE lug_events SET public_adults=25 WHERE id=" + std::to_string(id));
    auto withNums = GET("/about");
    expect_contains(withNums, "The past twelve months");
    expect_contains(withNums, "visitors welcomed");
    POST("/settings/about", "enabled=1&about_title=Meet+us&markdown=" + encode(md), admin_token);
    auto noNums = GET("/about");
    expect_contains(noNums, "<title>Meet us</title>");
    expect_not_contains(noNums, "The past twelve months");

    // Taking the photo out of the text deletes it
    POST("/settings/about", "enabled=1&markdown=Just+text", admin_token);
    EXPECT_EQ(GET(url).code, 404);
    EXPECT_EQ(one_int(*db, "SELECT COUNT(*) FROM about_photos"), 0);

    // Too long
    EXPECT_EQ(POST("/settings/about", "enabled=1&markdown=" + std::string(20001, 'x'), admin_token).code, 400);
    // Hide it again
    auto off = POST("/settings/about", "markdown=Just+text", admin_token);
    expect_contains(off, "hidden until");
    EXPECT_EQ(GET("/about").code, 404);
}
