// Settings > Features: switching optional parts of the app off and on.
#include "integration_test_base.hpp"
#include "services/Features.hpp"
#include "services/DuesService.hpp"
#include "repositories/NotificationPrefs.hpp"

namespace {
LugEvent feat_event(const std::string& scope = "lug_wide") {
    LugEvent e;
    e.title = "Feature Show"; e.start_time = "2099-07-01T09:00:00"; e.end_time = "2099-07-01T17:00:00";
    e.scope = scope; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    return e;
}
// Form body with every feature ticked except `off`.
std::string all_but(const std::vector<std::string>& off) {
    std::string body;
    for (const auto& f : Features::all()) {
        if (std::find(off.begin(), off.end(), f.key) != off.end()) continue;
        body += (body.empty() ? "" : "&") + std::string(f.key) + "=1";
    }
    return body;
}
}

TEST_F(IntegrationTest, FeaturesPageAdminOnlyAndDefaults) {
    EXPECT_EQ(GET("/settings/features", chapter_lead_token).code, 403);
    EXPECT_EQ(POST("/settings/features", "", chapter_lead_token).code, 403);
    auto r = GET("/settings/features", admin_token);
    EXPECT_EQ(r.code, 200);
    for (const auto& f : Features::all()) expect_contains(r, std::string("name=\"") + f.key + "\"");
    EXPECT_TRUE(Features::on("chapters"));
    EXPECT_FALSE(Features::on("public_shows"));       // opt-in
    expect_contains(GET("/dashboard", admin_token), "hx-get=\"/settings/features\"");
}

TEST_F(IntegrationTest, FeaturesChaptersOff) {
    auto ch_event = event_svc->create([&] { auto e = feat_event("chapter"); e.chapter_id = test_chapter_id; return e; }());
    EXPECT_EQ(GET("/chapters", member_token).code, 200);
    auto save = POST("/settings/features", all_but({"chapters"}), admin_token);
    EXPECT_EQ(save.code, 200);
    EXPECT_NE(save.headers.find("HX-Refresh: true"), std::string::npos);
    EXPECT_FALSE(Features::on("chapters"));

    EXPECT_EQ(GET("/chapters", member_token).code, 404);
    EXPECT_EQ(GET("/chapters/" + std::to_string(test_chapter_id), admin_token).code, 404);
    EXPECT_EQ(GET("/calendar/chapter/" + std::to_string(test_chapter_id) + "/feed.ics").code, 404);
    auto dash = GET("/dashboard", member_token);
    expect_not_contains(dash, "hx-get=\"/chapters\"");
    expect_contains(dash, "data-features-off=\"chapters\"");

    // New events default to LUG-wide and can't be created as chapter events
    auto form = GET_HTMX("/events/new", admin_token);
    expect_not_contains(form, "Chapter Event");
    POST("/events", "title=Scoped&start_time=2099-08-01&end_time=2099-08-01&scope=chapter", admin_token);
    auto q = db->prepare("SELECT scope FROM lug_events WHERE title='Scoped'");
    ASSERT_TRUE(q.step());
    EXPECT_EQ(q.col_text(0), "lug_wide");
    q.reset();
    // An existing chapter event keeps its scope choice when edited
    expect_contains(GET_HTMX("/events/" + std::to_string(ch_event.id) + "/edit", admin_token), "Chapter Event");

    // The API isn't affected
    std::string key = make_api_key("read");
    EXPECT_EQ(API_GET("/api/v1/chapters", key).code, 200);

    // Back on: everything is still there
    POST("/settings/features", all_but({}), admin_token);
    EXPECT_EQ(GET("/chapters/" + std::to_string(test_chapter_id), admin_token).code, 200);
    auto a = db->prepare("SELECT COUNT(*) FROM audit_log WHERE action='settings.features'");
    ASSERT_TRUE(a.step());
    EXPECT_EQ(a.col_int(0), 2);
}

TEST_F(IntegrationTest, FeaturesEachOwnsItsPages) {
    auto ev = event_svc->create(feat_event());
    std::string e = "/events/" + std::to_string(ev.id);
    struct Case { const char* key; std::string method, path; };
    std::vector<Case> cases = {
        {"dues", "GET", "/members/" + std::to_string(regular_member_id) + "/dues"},
        {"perks", "GET", "/perks"},
        {"rsvps", "GET", e + "/rsvp"},
        {"displays", "GET", e + "/displays"},
        {"shifts", "GET", e + "/shifts"},
        {"photos", "GET", e + "/photos"},
        {"challenges", "GET", "/challenges"},
        {"inventory", "GET", "/inventory"},
        {"treasury", "GET", "/treasury"},
        {"series", "GET", "/meetings/series"},
        {"reports", "GET", "/reports/annual"},
        {"kiosk", "GET", e + "/kiosk"},
        {"qr_checkin", "POST", e + "/generate-checkin"},
        {"calendar", "GET", "/calendar.ics"},
        {"discord_reports", "POST", e + "/publish-report"},
    };
    for (const auto& c : cases) {
        EXPECT_NE(http(c.method, c.path, "", admin_token).code, 404) << c.key << " on: " << c.path;
        Features::set(c.key, false);
        EXPECT_EQ(http(c.method, c.path, "", admin_token).code, 404) << c.key << " off: " << c.path;
        Features::set(c.key, true);
    }
    // Public shows is opt-in
    EXPECT_EQ(GET("/shows").code, 404);
    Features::set("public_shows", true);
    EXPECT_EQ(GET("/shows").code, 200);
}

TEST_F(IntegrationTest, FeaturesHidePanelsAndLinks) {
    auto ev = event_svc->create(feat_event());
    auto on = GET_HTMX("/events/" + std::to_string(ev.id), admin_token);
    for (const char* p : {"/photos\"", "/shifts\"", "/displays\"", "/rsvp\""}) expect_contains(on, p);
    POST("/settings/features", all_but({"photos", "shifts", "displays", "rsvps", "inventory", "challenges", "treasury"}), admin_token);
    auto off = GET_HTMX("/events/" + std::to_string(ev.id), admin_token);
    for (const char* p : {"/photos\"", "/shifts\"", "/displays\"", "/rsvp\""}) expect_not_contains(off, p);
    auto dash = GET("/dashboard", admin_token);
    for (const char* p : {"hx-get=\"/inventory\"", "hx-get=\"/challenges\"", "hx-get=\"/treasury\""}) expect_not_contains(dash, p);
}

TEST_F(IntegrationTest, FeaturesStopBackgroundWorkAndHideNotifications) {
    // Dues: no expiry or reminders while off
    {
        auto u = db->prepare("UPDATE members SET is_paid=1, paid_until='2000-01-01' WHERE id=?");
        u.bind(1, regular_member_id); u.step();
    }
    DuesRepository dues(*db);
    DuesService svc(dues, *settings_repo, *discord_client, *audit_svc);
    Features::set("dues", false);
    EXPECT_EQ(svc.run_once().expired, 0);
    Features::set("dues", true);
    EXPECT_EQ(svc.run_once().expired, 1);

    // Notification choices only list features that are on, and saving keeps hidden ones
    Features::set("inventory", false);
    auto acct = GET("/account", member_token);
    expect_not_contains(acct, "LUG items due back");
    expect_contains(acct, "Event reminders");
    NotificationPrefs(*db).set(regular_member_id, "loan_reminder", false);
    POST("/account/notifications", "event_reminder=1", member_token);
    EXPECT_FALSE(NotificationPrefs(*db).wants(regular_member_id, "loan_reminder"));
    NotificationPrefs(*db).set(regular_member_id, "loan_reminder", true);
    POST("/account/notifications", "event_reminder=1", member_token);
    EXPECT_TRUE(NotificationPrefs(*db).wants(regular_member_id, "loan_reminder"));
}
