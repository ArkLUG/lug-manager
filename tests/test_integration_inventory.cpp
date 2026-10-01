// LUG inventory and check-out.
#include "integration_test_base.hpp"

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
