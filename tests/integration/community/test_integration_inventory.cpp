// LUG inventory and check-out.
#include "integration_test_base.hpp"
#include "services/Features.hpp"

namespace {
int64_t item_id(SqliteDatabase& db, const std::string& name) {
    auto st = db.prepare("SELECT id FROM inventory_items WHERE name=?");
    st.bind(1, name);
    return st.step() ? st.col_int(0) : 0;
}
}

TEST_F(IntegrationTest, InventoryPermissions) {
    EXPECT_NE(GET("/inventory").code, 200);
    auto page = GET("/inventory", member_token);
    EXPECT_EQ(page.code, 200);
    expect_not_contains(page, "Add an item");
    EXPECT_EQ(POST("/inventory", "name=Table&quantity=2", member_token).code, 403);
    EXPECT_EQ(GET("/inventory.csv", member_token).code, 403);
    EXPECT_EQ(POST("/inventory", "name=Table&quantity=2", chapter_lead_token).code, 200);
    int64_t id = item_id(*db, "Table");
    ASSERT_GT(id, 0);
    EXPECT_EQ(POST("/inventory/checkout", "item_id=" + std::to_string(id) + "&member_id=" + std::to_string(regular_member_id) + "&quantity=1", member_token).code, 403);
    EXPECT_EQ(POST("/inventory/" + std::to_string(id), "name=Hacked&quantity=9", member_token).code, 403);
}

TEST_F(IntegrationTest, InventoryCheckoutAndReturn) {
    EXPECT_EQ(POST("/inventory", "name=&quantity=1", admin_token).code, 400);
    EXPECT_EQ(POST("/inventory", "name=Baseplate&quantity=0", admin_token).code, 400);
    auto add = POST("/inventory", "name=Baseplate+48x48&category=Display&quantity=10&location=Storage+unit", admin_token);
    EXPECT_EQ(add.code, 200);
    expect_contains(add, "Baseplate 48x48");
    std::string id = std::to_string(item_id(*db, "Baseplate 48x48"));
    std::string who = std::to_string(regular_member_id);

    EXPECT_EQ(POST("/inventory/checkout", "item_id=" + id + "&member_id=" + who + "&quantity=11", admin_token).code, 409);
    EXPECT_EQ(POST("/inventory/checkout", "item_id=" + id + "&member_id=999999&quantity=1", admin_token).code, 404);
    EXPECT_EQ(POST("/inventory/checkout", "item_id=" + id + "&member_id=" + who + "&quantity=1&due_on=bad", admin_token).code, 400);
    auto co = POST("/inventory/checkout", "item_id=" + id + "&member_id=" + who + "&quantity=8&due_on=2000-01-01&notes=Spring+show", admin_token);
    EXPECT_EQ(co.code, 200);
    expect_contains(co, ">2</span> <span class=\"text-gray-400\">of 10");
    expect_contains(co, "(overdue)");
    EXPECT_EQ(POST("/inventory/checkout", "item_id=" + id + "&member_id=" + who + "&quantity=3", admin_token).code, 409);

    // The borrower sees their own loan; others see none
    expect_contains(GET("/inventory", member_token), "Spring show");
    expect_not_contains(GET("/inventory", event_manager_token), "Spring show");
    // CSV for managers
    auto csv = GET("/inventory.csv", admin_token);
    EXPECT_EQ(csv.code, 200);
    expect_contains(csv, "Baseplate 48x48");
    expect_contains(csv, "Regular U.");

    // Can't shrink below what's out, or archive while out
    EXPECT_EQ(POST("/inventory/" + id, "name=Baseplate+48x48&quantity=5", admin_token).code, 400);
    EXPECT_EQ(POST("/inventory/" + id + "/archive", "", admin_token).code, 409);
    // Borrower can't delete their account while holding LUG items
    EXPECT_EQ(POST("/account/delete", "confirm=DELETE", member_token).code, 409);

    auto lq = db->prepare("SELECT id FROM inventory_loans WHERE returned_at IS NULL");
    ASSERT_TRUE(lq.step());
    std::string loan = std::to_string(lq.col_int(0));
    lq.reset();
    EXPECT_EQ(POST("/inventory/loans/" + loan + "/return", "", member_token).code, 403);
    EXPECT_EQ(POST("/inventory/loans/" + loan + "/return", "", admin_token).code, 200);
    EXPECT_EQ(POST("/inventory/loans/" + loan + "/return", "", admin_token).code, 404);

    EXPECT_EQ(POST("/inventory/" + id, "name=Baseplate+48x48&quantity=5", admin_token).code, 200);
    EXPECT_EQ(POST("/inventory/" + id + "/archive", "", admin_token).code, 200);
    expect_not_contains(GET("/inventory", admin_token), "Baseplate 48x48");
    auto a = db->prepare("SELECT COUNT(*) FROM audit_log WHERE action LIKE 'inventory.%'");
    ASSERT_TRUE(a.step());
    EXPECT_GE(a.col_int(0), 5);
}

#include "services/notifications/LoanReminders.hpp"
#include "repositories/members/NotificationPrefs.hpp"

TEST_F(IntegrationTest, InventoryOverdueReminderOncePerLoan) {
    Member m;
    m.first_name = "Bo"; m.last_name = "Rower"; m.display_name = "Bo R."; m.email = "bo@example.org"; m.role = "member";
    int64_t bo = member_repo->create(m).id;
    ASSERT_EQ(POST("/inventory", "name=Banner&quantity=2", admin_token).code, 200);
    std::string id = std::to_string(item_id(*db, "Banner"));
    ASSERT_EQ(POST("/inventory/checkout", "item_id=" + id + "&member_id=" + std::to_string(bo) + "&quantity=1&due_on=2030-05-01", admin_token).code, 200);
    ASSERT_EQ(POST("/inventory/checkout", "item_id=" + id + "&member_id=" + std::to_string(regular_member_id) + "&quantity=1&due_on=2030-06-01", admin_token).code, 200);

    LoanReminders lr(*db, std::make_shared<Notifier>(*db, *discord_client, mailer, "http://lug.test"));
    EXPECT_EQ(lr.run_once("2030-04-30"), 0);           // not due yet
    lr.run_once("2030-05-02");                          // Bo's is overdue (email); regular's isn't due
    auto out = mailer->outbox();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].to, "bo@example.org");
    EXPECT_EQ(out[0].subject, "Please return: Banner");
    EXPECT_NE(out[0].body.find("due back on 2030-05-01"), std::string::npos);
    EXPECT_NE(out[0].unsubscribe_url.find("kind=loan_reminder"), std::string::npos);
    lr.run_once("2030-05-03");                          // only once
    EXPECT_EQ(mailer->outbox().size(), 1u);

    // Opted-out borrowers aren't messaged (but the loan is still marked)
    NotificationPrefs(*db).set(regular_member_id, "loan_reminder", false);
    mailer->clear_outbox();
    lr.run_once("2030-06-01");
    EXPECT_TRUE(mailer->outbox().empty());
}

#include "db/Migrations.hpp"
#include <filesystem>

namespace {
int64_t loc_id(SqliteDatabase& db, const std::string& name) {
    auto st = db.prepare("SELECT id FROM storage_locations WHERE name=?");
    st.bind(1, name);
    return st.step() ? st.col_int(0) : 0;
}
int64_t stock(SqliteDatabase& db, int64_t item, int64_t loc) {
    auto st = db.prepare("SELECT COALESCE(SUM(quantity),0) FROM inventory_stock WHERE item_id=? AND location_id=?");
    st.bind(1, item); st.bind(2, loc);
    return st.step() ? st.col_int(0) : 0;
}
}

TEST(InventoryMigration, KeptAtTextBecomesLocations) {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / ("mig065-" + std::to_string(getpid()));
    fs::create_directories(dir);
    for (const auto& e : fs::directory_iterator("sql/migrations"))
        if (e.path().filename().string() < "065") fs::copy(e.path(), dir / e.path().filename());
    SqliteDatabase db(":memory:");
    Migrations m(db);
    m.run(dir.string());                                   // up to 064
    db.execute("INSERT INTO members (id, display_name, role) VALUES (1, 'A', 'member')");
    db.execute("INSERT INTO inventory_items (id, name, quantity, location) VALUES (1, 'Table', 8, 'Storage unit 14'), "
               "(2, 'Banner', 2, ' Storage unit 14 '), (3, 'Case', 1, ''), (4, 'Cord', 3, 'Trailer')");
    db.execute("INSERT INTO inventory_loans (item_id, member_id, quantity) VALUES (1, 1, 3)");
    m.run("sql/migrations");                               // applies 065
    auto st = db.prepare("SELECT COUNT(*) FROM storage_locations");
    ASSERT_TRUE(st.step());
    EXPECT_EQ(st.col_int(0), 2);                           // "Storage unit 14" and "Trailer", trimmed and de-duplicated
    st.reset();
    int64_t unit = loc_id(db, "Storage unit 14"), trailer = loc_id(db, "Trailer");
    EXPECT_EQ(stock(db, 1, unit), 5);                       // 8 owned - 3 on loan
    EXPECT_EQ(stock(db, 2, unit), 2);
    EXPECT_EQ(stock(db, 4, trailer), 3);
    auto none = db.prepare("SELECT COUNT(*) FROM inventory_stock WHERE item_id=3");
    ASSERT_TRUE(none.step());
    EXPECT_EQ(none.col_int(0), 0);                          // no "kept at" -> not placed
    none.reset();
    auto from = db.prepare("SELECT from_location_id FROM inventory_loans WHERE item_id=1");
    ASSERT_TRUE(from.step());
    EXPECT_EQ(from.col_int(0), unit);                      // open loan returns to where it was kept
    fs::remove_all(dir);
}

TEST_F(IntegrationTest, InventoryLocationsKeepersAndMoves) {
    // Locations: managers only, keeper must exist
    EXPECT_EQ(POST("/inventory/locations", "name=Trailer&kind=trailer", member_token).code, 403);
    EXPECT_EQ(POST("/inventory/locations", "name=&kind=trailer", admin_token).code, 400);
    EXPECT_EQ(POST("/inventory/locations", "name=X&kind=castle", admin_token).code, 400);
    EXPECT_EQ(POST("/inventory/locations", "name=X&kind=trailer&keeper_id=999999", admin_token).code, 404);
    EXPECT_EQ(POST("/inventory/locations", "name=Storage+unit&kind=storage&address=12+Main+St", admin_token).code, 200);
    auto t = POST("/inventory/locations", "name=Show+trailer&kind=trailer&keeper_id=" + std::to_string(regular_member_id), admin_token);
    EXPECT_EQ(t.code, 200);
    expect_contains(t, "looked after by Regular U.");
    int64_t unit = loc_id(*db, "Storage unit"), trailer = loc_id(*db, "Show trailer");

    // Add 40 baseplates at the unit, move 15 to the trailer
    ASSERT_EQ(POST("/inventory", "name=Baseplate&quantity=40&location_id=" + std::to_string(unit), admin_token).code, 200);
    int64_t item = item_id(*db, "Baseplate");
    EXPECT_EQ(stock(*db, item, unit), 40);
    std::string mv = "/inventory/" + std::to_string(item) + "/move";
    EXPECT_EQ(POST(mv, "quantity=15&from_location_id=" + std::to_string(unit) + "&to_location_id=" + std::to_string(trailer), member_token).code, 403);
    EXPECT_EQ(POST(mv, "quantity=50&from_location_id=" + std::to_string(unit) + "&to_location_id=" + std::to_string(trailer), admin_token).code, 409);
    EXPECT_EQ(POST(mv, "quantity=5&from_location_id=" + std::to_string(unit) + "&to_location_id=" + std::to_string(unit), admin_token).code, 400);
    auto moved = POST(mv, "quantity=15&from_location_id=" + std::to_string(unit) + "&to_location_id=" + std::to_string(trailer), admin_token);
    EXPECT_EQ(moved.code, 200);
    expect_contains(moved, "Moved 15 × Baseplate to Show trailer.");
    EXPECT_EQ(stock(*db, item, unit), 25);
    EXPECT_EQ(stock(*db, item, trailer), 15);

    // The keeper sees what they look after; filtering by location
    expect_contains(GET("/inventory", member_token), "You look after:");
    auto at = GET("/inventory?location=" + std::to_string(trailer), admin_token);
    expect_contains(at, "Items at Show trailer");
    expect_contains(at, "15 · Show trailer");

    // Check-out takes from a location; return goes back there by default, or elsewhere
    std::string co = "item_id=" + std::to_string(item) + "&member_id=" + std::to_string(regular_member_id);
    EXPECT_EQ(POST("/inventory/checkout", co + "&quantity=20&from_location_id=" + std::to_string(trailer), admin_token).code, 409);
    ASSERT_EQ(POST("/inventory/checkout", co + "&quantity=10&from_location_id=" + std::to_string(trailer), admin_token).code, 200);
    EXPECT_EQ(stock(*db, item, trailer), 5);
    ASSERT_EQ(POST("/inventory/checkout", co + "&quantity=20", admin_token).code, 200);   // "wherever": the unit has 25
    EXPECT_EQ(stock(*db, item, unit), 5);
    auto lq = db->prepare("SELECT id FROM inventory_loans WHERE returned_at IS NULL ORDER BY id");
    ASSERT_TRUE(lq.step());
    std::string first = std::to_string(lq.col_int(0));
    ASSERT_TRUE(lq.step());
    std::string second = std::to_string(lq.col_int(0));
    lq.reset();
    auto back = POST("/inventory/loans/" + first + "/return", "", admin_token);
    expect_contains(back, "returned to Show trailer");
    EXPECT_EQ(stock(*db, item, trailer), 15);
    POST("/inventory/loans/" + second + "/return", "to_location_id=" + std::to_string(trailer), admin_token);
    EXPECT_EQ(stock(*db, item, trailer), 35);
    EXPECT_EQ(stock(*db, item, unit), 5);

    // Total can't drop below what's placed + on loan; unplaced items can be placed
    EXPECT_EQ(POST("/inventory/" + std::to_string(item), "name=Baseplate&quantity=30", admin_token).code, 400);
    ASSERT_EQ(POST("/inventory/" + std::to_string(item), "name=Baseplate&quantity=45", admin_token).code, 200);
    expect_contains(GET("/inventory", admin_token), "5 not placed");
    EXPECT_EQ(POST(mv, "quantity=5&from_location_id=0&to_location_id=" + std::to_string(unit), admin_token).code, 200);
    EXPECT_EQ(stock(*db, item, unit), 10);

    // CSV lists each place; a location with things in it can't be removed
    auto csv = GET("/inventory.csv", admin_token);
    expect_contains(csv, "\"Show trailer\",\"35\",\"Regular U.\"");
    EXPECT_EQ(POST("/inventory/locations/" + std::to_string(trailer) + "/archive", "", admin_token).code, 409);
    POST(mv, "quantity=35&from_location_id=" + std::to_string(trailer) + "&to_location_id=" + std::to_string(unit), admin_token);
    EXPECT_EQ(POST("/inventory/locations/" + std::to_string(trailer) + "/archive", "", admin_token).code, 200);
    expect_not_contains(GET("/inventory", admin_token), ">Show trailer<");

    // Editing a location (keeper change) is audited
    EXPECT_EQ(POST("/inventory/locations/" + std::to_string(unit), "name=Storage+unit+14&kind=storage&keeper_id=" +
                   std::to_string(admin_member_id), admin_token).code, 200);
    auto a = db->prepare("SELECT COUNT(*) FROM audit_log WHERE action LIKE 'inventory.location_%' OR action='inventory.move'");
    ASSERT_TRUE(a.step());
    EXPECT_GE(a.col_int(0), 5);
}

#include "services/members/MemberMerge.hpp"
TEST_F(IntegrationTest, InventoryKeeperFollowsMemberMerge) {
    Member d; d.first_name = "Dup"; d.last_name = "Keeper"; d.display_name = "Dup K."; d.role = "member";
    int64_t dup = member_repo->create(d).id;
    ASSERT_EQ(POST("/inventory/locations", "name=Garage&kind=home&keeper_id=" + std::to_string(dup), admin_token).code, 200);
    MemberMerge(*db).merge(regular_member_id, dup);
    auto st = db->prepare("SELECT keeper_id FROM storage_locations WHERE name='Garage'");
    ASSERT_TRUE(st.step());
    EXPECT_EQ(st.col_int(0), regular_member_id);
}


TEST_F(IntegrationTest, InventoryPhotoAndCondition) {
    ASSERT_EQ(POST("/inventory", "name=Display+case&quantity=2", admin_token).code, 200);
    std::string id = std::to_string(item_id(*db, "Display case"));
    EXPECT_EQ(POST_FILE("/inventory/" + id + "/photo", "photo", "c.png", kTinyPng, member_token).code, 403);
    EXPECT_EQ(POST_FILE("/inventory/" + id + "/photo", "photo", "c.svg", "<svg/>", admin_token).code, 400);
    auto up = POST_FILE("/inventory/" + id + "/photo", "photo", "c.png", kTinyPng, admin_token);
    EXPECT_EQ(up.code, 200);
    auto f = db->prepare("SELECT photo_file FROM inventory_items WHERE id=?");
    f.bind(1, static_cast<int64_t>(std::stoll(id)));
    ASSERT_TRUE(f.step());
    std::string file = f.col_text(0);
    f.reset();
    ASSERT_FALSE(file.empty());
    expect_contains(GET("/inventory", member_token), "/uploads/" + file);
    EXPECT_EQ(GET("/uploads/" + file, member_token).code, 200);

    // Condition shows as a badge with its note
    ASSERT_EQ(POST("/inventory/" + id, "name=Display+case&quantity=2&condition=needs_repair&condition_note=Cracked+lid", admin_token).code, 200);
    auto page = GET("/inventory", member_token);
    expect_contains(page, "Needs repair");
    expect_contains(page, "Cracked lid");
    POST("/inventory/" + id, "name=Display+case&quantity=2&condition=bogus", admin_token);   // unknown -> good
    expect_not_contains(GET("/inventory", member_token), "Needs repair");

    // Removing the photo deletes the file
    EXPECT_EQ(POST("/inventory/" + id + "/photo/delete", "", admin_token).code, 200);
    EXPECT_EQ(GET("/uploads/" + file, member_token).code, 404);
}

TEST_F(IntegrationTest, EventPackList) {
    LugEvent e;
    e.title = "Pack Show"; e.start_time = "2099-07-01T09:00:00"; e.end_time = "2099-07-01T17:00:00";
    e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true;
    auto ev = event_svc->create(e);
    std::string base = "/events/" + std::to_string(ev.id) + "/pack";
    ASSERT_EQ(POST("/inventory/locations", "name=Trailer&kind=trailer", admin_token).code, 200);
    int64_t trailer = loc_id(*db, "Trailer");
    ASSERT_EQ(POST("/inventory", "name=Table&quantity=4&location_id=" + std::to_string(trailer), admin_token).code, 200);
    ASSERT_EQ(POST("/inventory", "name=Banner&quantity=1", admin_token).code, 200);
    std::string table = std::to_string(item_id(*db, "Table")), banner = std::to_string(item_id(*db, "Banner"));
    POST("/inventory/" + banner, "name=Banner&quantity=1&condition=worn&condition_note=Faded", admin_token);

    // Event managers build it; members can't change it
    EXPECT_EQ(POST(base, "item_id=" + table + "&quantity=3", member_token).code, 403);
    auto add = POST(base, "item_id=" + table + "&quantity=3&note=For+the+train+layout", admin_token);
    EXPECT_EQ(add.code, 200);
    expect_contains(add, "3 &times; Table");
    expect_contains(add, "4 at Trailer");                       // where to fetch it
    expect_contains(add, "For the train layout");
    auto over = POST(base, "item_id=" + banner + "&quantity=2", admin_token);
    expect_contains(over, "The LUG only owns 1.");
    expect_contains(over, "Worn - Faded");
    EXPECT_EQ(POST(base, "item_id=999999&quantity=1", admin_token).code, 404);
    EXPECT_EQ(POST(base, "item_id=" + table + "&quantity=0", admin_token).code, 400);
    expect_contains(POST(base, "item_id=" + table + "&quantity=4", admin_token), "4 &times; Table");   // update, not duplicate

    // Packing
    expect_contains(GET(base, member_token), "0 of 2 packed");
    EXPECT_EQ(POST(base + "/" + table + "/toggle", "", member_token).code, 403);
    expect_contains(POST(base + "/" + table + "/toggle", "", admin_token), "1 of 2 packed");
    expect_contains(POST(base + "/" + banner + "/toggle", "", admin_token), "2 of 2 packed");
    auto print = GET(base + "/print", member_token);
    EXPECT_EQ(print.code, 200);
    expect_contains(print, "Pack list: Pack Show");
    expect_contains(print, "4 at Trailer");
    EXPECT_EQ(POST(base + "/" + banner + "/remove", "", admin_token).code, 200);
    expect_contains(GET(base, admin_token), "1 of 1 packed");

    // Event page shows the panel to managers; switching inventory off hides it and its pages
    expect_contains(GET_HTMX("/events/" + std::to_string(ev.id), admin_token), "/pack\"");
    Features::set("inventory", false);
    EXPECT_EQ(GET(base, admin_token).code, 404);
    expect_not_contains(GET_HTMX("/events/" + std::to_string(ev.id), admin_token), "/pack\"");
}
