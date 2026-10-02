// Integration tests for the admin-uploaded logo (BrandingRoutes.cpp): page
// access, valid upload + sidebar propagation (the favicon stays ours), rejection of
// non-image/oversized uploads, and removal reverting to default branding.
#include "integration_test_base.hpp"


TEST_F(IntegrationTest, BrandingPageRequiresAdmin) {
    auto r = GET("/settings/branding", member_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
}

TEST_F(IntegrationTest, BrandingPageLoadsForAdmin) {
    auto r = GET("/settings/branding", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Logo");
    expect_contains(r, "No logo set");
}

TEST_F(IntegrationTest, BrandingLogoMissingReturns404) {
    auto r = GET("/branding/logo");
    EXPECT_EQ(r.code, 404);
}

TEST_F(IntegrationTest, BrandingUploadPngSucceedsAndIsServed) {
    auto r = POST_FILE("/settings/branding", "logo", "mylogo.png", kTinyPng, admin_token);
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.location.find("/settings/branding"), std::string::npos);

    // Persisted setting reflects the upload.
    EXPECT_EQ(settings_repo->get("branding_logo_extension"), ".png");
    EXPECT_EQ(settings_repo->get("branding_logo_content_type"), "image/png");

    // Served back correctly, with the right content-type and exact bytes.
    auto served = GET("/branding/logo");
    EXPECT_EQ(served.code, 200);
    EXPECT_EQ(served.body, kTinyPng);

    // The branding settings page itself now shows the logo, not the placeholder.
    auto page = GET("/settings/branding", admin_token);
    expect_contains(page, "Currently using a custom logo");
    expect_contains(page, "Remove Logo");
}

TEST_F(IntegrationTest, BrandingUploadPropagatesToSidebar) {
    auto up = POST_FILE("/settings/branding", "logo", "mylogo.png", kTinyPng, admin_token);
    EXPECT_EQ(up.code, 200);

    // Any authenticated page (not just the branding settings page itself)
    // should now show the custom logo in the sidebar - this is the whole
    // point of the feature, and it's built
    // via a shared middleware pointer rather than being wired per-page, so
    // this specifically exercises that it actually reaches an unrelated page.
    auto dash = GET("/dashboard", admin_token);
    EXPECT_EQ(dash.code, 200);
    // Crow's mustache HTML-escapes interpolated values by default (/ -> &#x2F;,
    // = -> &#x3D;), which is fine for an href - browsers decode entities in
    // attribute values normally. Assert on the escaped form actually emitted.
    expect_contains(dash, "branding&#x2F;logo");
    // The tab icon stays LUG Manager's own logo
    expect_contains(dash, "<link rel=\"icon\" href=\"/static/logo.svg");
    expect_not_contains(dash, "<link rel=\"icon\" href=\"&#x2F;branding");
}

TEST_F(IntegrationTest, BrandingUploadRejectsNonImage) {
    auto r = POST_FILE("/settings/branding", "logo", "notes.txt",
                        "just some plain text, not an image", admin_token);
    EXPECT_EQ(r.code, 200); // renders an inline error, not an HTTP error code
    expect_contains(r, "doesn't look like an image file");
    EXPECT_TRUE(settings_repo->get("branding_logo_extension").empty());
}

TEST_F(IntegrationTest, BrandingUploadRejectsOversizedFile) {
    std::string huge(6 * 1024 * 1024, 'A'); // 6 MB, over the 5 MB cap
    // Prefix with real PNG magic bytes so it would otherwise pass the
    // image-sniff check - proves the size cap is enforced independently.
    huge.replace(0, 8, kTinyPng.substr(0, 8));
    auto r = POST_FILE("/settings/branding", "logo", "big.png", huge, admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "too large");
    EXPECT_TRUE(settings_repo->get("branding_logo_extension").empty());
}

TEST_F(IntegrationTest, BrandingUploadRequiresAdmin) {
    auto r = POST_FILE("/settings/branding", "logo", "mylogo.png", kTinyPng, member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, BrandingRemoveRevertToDefault) {
    auto up = POST_FILE("/settings/branding", "logo", "mylogo.png", kTinyPng, admin_token);
    EXPECT_EQ(up.code, 200);
    EXPECT_FALSE(settings_repo->get("branding_logo_extension").empty());

    auto rm = POST_HTMX("/settings/branding/remove", "", admin_token);
    EXPECT_EQ(rm.code, 200);
    EXPECT_TRUE(settings_repo->get("branding_logo_extension").empty());

    auto served = GET("/branding/logo");
    EXPECT_EQ(served.code, 404);

    auto page = GET("/settings/branding", admin_token);
    expect_contains(page, "No logo set");
}

TEST_F(IntegrationTest, BrandingRemoveRequiresAdmin) {
    auto r = POST("/settings/branding/remove", "", member_token);
    EXPECT_EQ(r.code, 403);
}
