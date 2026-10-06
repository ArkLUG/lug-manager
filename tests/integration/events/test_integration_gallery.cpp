// Event photo galleries and build challenges.
#include "integration_test_base.hpp"
#include "services/PhotoStore.hpp"
#include "services/UploadsInUse.hpp"

namespace {
// Valid 1x1 PNG

LugEvent gallery_event() {
    LugEvent e;
    e.title = "Photo Show";
    e.start_time = "2099-07-01T09:00:00";
    e.end_time = "2099-07-01T17:00:00";
    e.scope = "lug_wide";
    e.status = "confirmed";
    e.suppress_discord = true;
    e.suppress_calendar = true;
    return e;
}

std::string first_upload(const std::string& body) {
    auto p = body.find("/uploads/");
    if (p == std::string::npos) return "";
    auto e = body.find('"', p);
    return body.substr(p, e - p);
}

std::string ymd_offset(int days) {
    time_t t = time(nullptr) + days * 86400;
    struct tm tm{};
    localtime_r(&t, &tm);
    char b[16];
    strftime(b, sizeof(b), "%Y-%m-%d", &tm);
    return b;
}
}

TEST_F(IntegrationTest, GalleryUploadServeDelete) {
    auto ev = event_svc->create(gallery_event());
    std::string base = "/events/" + std::to_string(ev.id) + "/photos";

    // Not logged in
    EXPECT_EQ(POST_FILE(base, "photo", "a.png", kTinyPng).code == 200, false);
    // SVG / junk rejected
    auto bad = POST_FILE(base, "photo", "x.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>", member_token);
    EXPECT_EQ(bad.code, 400);

    auto ok = POST_FILE(base, "photo", "a.png", kTinyPng, member_token, {{"caption", "My <b>MOC</b>"}});
    EXPECT_EQ(ok.code, 200);
    expect_contains(ok, "Photo added.");
    expect_contains(ok, "My &lt;b&gt;MOC");
    expect_not_contains(ok, "<b>MOC");
    std::string url = first_upload(ok.body);
    ASSERT_FALSE(url.empty());

    // Served to members only, sandboxed, with an image type
    EXPECT_NE(GET(url).code, 200);
    auto img = GET(url, chapter_lead_token);
    EXPECT_EQ(img.code, 200);
    EXPECT_EQ(img.body.substr(0, 4), "\x89PNG");
    EXPECT_NE(img.headers.find("image/png"), std::string::npos);
    EXPECT_NE(img.headers.find("sandbox"), std::string::npos);
    EXPECT_EQ(GET("/uploads/..%2Flug.db", admin_token).code, 404);

    auto q = db->prepare("SELECT id FROM event_photos WHERE event_id=?");
    q.bind(1, ev.id);
    ASSERT_TRUE(q.step());
    std::string del = base + "/" + std::to_string(q.col_int(0)) + "/delete";
    // Another member can't delete it; the uploader (or a manager) can
    EXPECT_EQ(POST(del, "", event_manager_token).code, 403);
    EXPECT_EQ(POST(del, "", member_token).code, 200);
    EXPECT_EQ(GET(url, admin_token).code, 404);
    expect_contains(GET(base, member_token), "No photos yet.");
}

TEST_F(IntegrationTest, GalleryAdminDeletesAnyPhoto) {
    auto ev = event_svc->create(gallery_event());
    std::string base = "/events/" + std::to_string(ev.id) + "/photos";
    ASSERT_EQ(POST_FILE(base, "photo", "a.png", kTinyPng, member_token).code, 200);
    auto q = db->prepare("SELECT id FROM event_photos WHERE event_id=?");
    q.bind(1, ev.id);
    ASSERT_TRUE(q.step());
    EXPECT_EQ(POST(base + "/" + std::to_string(q.col_int(0)) + "/delete", "", admin_token).code, 200);
}

TEST_F(IntegrationTest, ChallengeLifecycle) {
    // Only admins create
    std::string form = "title=Microscale&description=Tiny&starts_on=" + ymd_offset(-1) + "&ends_on=" + ymd_offset(3);
    EXPECT_EQ(POST("/challenges", form, member_token).code, 403);
    EXPECT_EQ(POST("/challenges", "title=X&starts_on=2099-01-05&ends_on=2099-01-01", admin_token).code, 400);
    auto c = POST("/challenges", form, admin_token);
    EXPECT_EQ(c.code, 200);
    auto q = db->prepare("SELECT id FROM challenges WHERE title='Microscale'");
    ASSERT_TRUE(q.step());
    std::string cid = std::to_string(q.col_int(0));
    std::string base = "/challenges/" + cid;
    expect_contains(GET("/challenges", member_token), "Open for entries");

    // Enter: needs a title, one entry per member
    EXPECT_EQ(POST_FILE(base + "/entries", "photo", "a.png", kTinyPng, member_token).code, 400);
    auto e1 = POST_FILE(base + "/entries", "photo", "a.png", kTinyPng, member_token, {{"title", "Tiny Castle"}});
    EXPECT_EQ(e1.code, 200);
    expect_contains(e1, "Tiny Castle");
    EXPECT_EQ(POST_FILE(base + "/entries", "photo", "b.png", kTinyPng, member_token, {{"title", "Again"}}).code, 409);
    ASSERT_EQ(POST_FILE(base + "/entries", "photo", "c.png", kTinyPng, chapter_lead_token, {{"title", "Tiny Ship"}}).code, 200);

    auto eq = db->prepare("SELECT id, member_id FROM challenge_entries WHERE challenge_id=? ORDER BY id");
    eq.bind(1, q.col_int(0));
    ASSERT_TRUE(eq.step());
    std::string castle = std::to_string(eq.col_int(0));
    ASSERT_TRUE(eq.step());
    std::string ship = std::to_string(eq.col_int(0));

    // No voting for yourself; one vote per member that moves / toggles
    EXPECT_EQ(POST(base + "/entries/" + castle + "/vote", "", member_token).code, 409);
    EXPECT_EQ(POST(base + "/entries/" + castle + "/vote", "", admin_token).code, 200);
    EXPECT_EQ(POST(base + "/entries/" + ship + "/vote", "", admin_token).code, 200);
    auto vc = db->prepare("SELECT COUNT(*), MAX(entry_id) FROM challenge_votes WHERE member_id=?");
    vc.bind(1, admin_member_id);
    ASSERT_TRUE(vc.step());
    EXPECT_EQ(vc.col_int(0), 1);
    EXPECT_EQ(std::to_string(vc.col_int(1)), ship);
    EXPECT_EQ(POST(base + "/entries/" + ship + "/vote", "", admin_token).code, 200); // toggle off
    auto vc2 = db->prepare("SELECT COUNT(*) FROM challenge_votes");
    ASSERT_TRUE(vc2.step());
    EXPECT_EQ(vc2.col_int(0), 0);

    // Results not out yet: counts hidden, can't announce
    auto hidden = GET(base, member_token);
    expect_not_contains(hidden, " vote<");
    expect_not_contains(hidden, " votes<");
    EXPECT_EQ(POST(base + "/announce", "", member_token).code, 403);
    EXPECT_EQ(POST(base + "/announce", "", admin_token).code, 409);

    // Someone else can't remove my entry; I can while entries are open
    EXPECT_EQ(POST(base + "/entries/" + castle + "/delete", "", chapter_lead_token).code, 403);
    EXPECT_EQ(POST(base + "/entries/" + castle + "/delete", "", member_token).code, 200);
}

TEST_F(IntegrationTest, ChallengeClosedPhases) {
    // Finished long ago: no entries, no votes, results visible
    auto ins = db->prepare("INSERT INTO challenges (title, starts_on, ends_on, created_by) VALUES ('Old','2020-01-01','2020-01-10',?) RETURNING id");
    ins.bind(1, admin_member_id);
    ASSERT_TRUE(ins.step());
    int64_t cid = ins.col_int(0);
    auto ent = db->prepare("INSERT INTO challenge_entries (challenge_id, member_id, title, file) VALUES (?,?,'Winner','x.png') RETURNING id");
    ent.bind(1, cid); ent.bind(2, regular_member_id);
    ASSERT_TRUE(ent.step());
    int64_t eid = ent.col_int(0);
    auto v = db->prepare("INSERT INTO challenge_votes (challenge_id, entry_id, member_id) VALUES (?,?,?)");
    v.bind(1, cid); v.bind(2, eid); v.bind(3, admin_member_id);
    v.step();

    std::string base = "/challenges/" + std::to_string(cid);
    EXPECT_EQ(POST_FILE(base + "/entries", "photo", "a.png", kTinyPng, chapter_lead_token, {{"title", "Late"}}).code, 409);
    EXPECT_EQ(POST(base + "/entries/" + std::to_string(eid) + "/vote", "", chapter_lead_token).code, 409);
    auto page = GET(base, member_token);
    expect_contains(page, "· 1 vote<");
    expect_contains(GET("/challenges", member_token), "Finished");
    // Owner can't remove after entries close; admin can
    EXPECT_EQ(POST(base + "/entries/" + std::to_string(eid) + "/delete", "", member_token).code, 403);
    // Announce runs (Discord is stubbed in tests, so it reports a failed post rather than crashing)
    EXPECT_EQ(POST(base + "/announce", "", admin_token).code, 200);
}

// A challenge can be edited and deleted (with its entries); the entry photos
// are then cleared out by the uploads sweep.
TEST_F(IntegrationTest, ChallengeEditDeleteAndPhotoSweep) {
    POST("/challenges", "title=Old+Name&starts_on=" + ymd_offset(-1) + "&ends_on=" + ymd_offset(3), admin_token);
    auto q = db->prepare("SELECT id FROM challenges WHERE title='Old Name'");
    ASSERT_TRUE(q.step());
    const std::string base = "/challenges/" + std::to_string(q.col_int(0));
    expect_contains(GET(base, admin_token), "hx-post=\"" + base + "/edit\"");
    expect_not_contains(GET(base, member_token), base + "/delete");

    EXPECT_EQ(POST(base + "/edit", "title=New+Name&starts_on=" + ymd_offset(-1) + "&ends_on=" + ymd_offset(5), member_token).code, 403);
    EXPECT_EQ(POST(base + "/edit", "title=&starts_on=x&ends_on=y", admin_token).code, 400);
    EXPECT_LT(POST(base + "/edit", "title=New+Name&description=Rules&starts_on=" + ymd_offset(-1) + "&ends_on=" + ymd_offset(5), admin_token).code, 400);
    expect_contains(GET(base, admin_token), "New Name");

    ASSERT_EQ(POST_FILE(base + "/entries", "photo", "a.png", kTinyPng, member_token, {{"title", "Tiny"}}).code, 200);
    auto f = db->prepare("SELECT file FROM challenge_entries");
    ASSERT_TRUE(f.step());
    const std::string file = f.col_text(0);
    f.reset();
    PhotoStore store(data_dir);
    std::string bytes;
    ASSERT_TRUE(store.read(file, bytes));

    EXPECT_EQ(POST(base + "/delete", "", member_token).code, 403);
    EXPECT_LT(POST(base + "/delete", "", admin_token).code, 400);
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM challenge_entries"), 0);
    expect_contains(GET("/audit", admin_token), "challenge.delete");
    // The sweep only takes files nothing uses, and not brand-new ones
    EXPECT_EQ(store.sweep(uploads_in_use(*db)), 0);
    EXPECT_TRUE(store.read(file, bytes));
    EXPECT_EQ(store.sweep(uploads_in_use(*db), std::chrono::seconds(-1)), 1);
    EXPECT_FALSE(store.read(file, bytes));
}

// Making a meeting or event says when Discord pings are switched off.
TEST_F(IntegrationTest, FormsWarnWhenPingsAreOff) {
    discord_client->set_suppress_pings(true);
    expect_contains(GET_HTMX("/meetings/new", admin_token), "Discord pings are off");
    expect_contains(GET_HTMX("/events/new", admin_token), "Discord pings are off");
    discord_client->set_suppress_pings(false);
    expect_not_contains(GET_HTMX("/meetings/new", admin_token), "Discord pings are off");
}
