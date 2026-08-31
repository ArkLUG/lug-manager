// Integration tests for the admin-uploaded logo (BrandingRoutes.cpp): page
// access, valid upload + sidebar/favicon propagation, rejection of
// non-image/oversized uploads, and removal reverting to default branding.
#include "integration_test_base.hpp"

namespace {
// Smallest valid PNG: a 1x1 transparent pixel. Real magic bytes (89 50 4E 47
// 0D 0A 1A 0A) followed by a minimal IHDR/IDAT/IEND chain - this is the
// well-known "smallest possible PNG" byte sequence, not a crafted fake.
const std::string kTinyPng = std::string(
    "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a"
    "\x00\x00\x00\x0d\x49\x48\x44\x52"
    "\x00\x00\x00\x01\x00\x00\x00\x01"
    "\x08\x06\x00\x00\x00\x1f\x15\xc4"
    "\x89\x00\x00\x00\x0a\x49\x44\x41"
    "\x54\x78\x9c\x63\x00\x01\x00\x00"
    "\x05\x00\x01\x0d\x0a\x2d\xb4\x00"
    "\x00\x00\x00\x49\x45\x4e\x44\xae"
    "\x42\x60\x82", 67);
}

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

TEST_F(IntegrationTest, BrandingUploadPropagatesToSidebarAndFavicon) {
    auto up = POST_FILE("/settings/branding", "logo", "mylogo.png", kTinyPng, admin_token);
    EXPECT_EQ(up.code, 200);

    // Any authenticated page (not just the branding settings page itself)
    // should now show the custom logo in the sidebar and reference it as
    // the favicon - this is the whole point of the feature, and it's built
    // via a shared middleware pointer rather than being wired per-page, so
    // this specifically exercises that it actually reaches an unrelated page.
    auto dash = GET("/dashboard", admin_token);
    EXPECT_EQ(dash.code, 200);
    // Crow's mustache HTML-escapes interpolated values by default (/ -> &#x2F;,
    // = -> &#x3D;), which is fine for an href - browsers decode entities in
    // attribute values normally. Assert on the escaped form actually emitted.
    expect_contains(dash, "branding&#x2F;logo");
    expect_contains(dash, "rel=\"icon\"");
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
