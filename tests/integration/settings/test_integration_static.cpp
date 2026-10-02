// /static serving: caching, ETags, compression, path safety.
#include "integration_test_base.hpp"
#include "routes/pages/HealthRoutes.hpp"

TEST_F(IntegrationTest, StaticVersionedAssetsAreImmutable) {
    auto r = GET("/static/theme.js?v=123");
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.headers.find("immutable"), std::string::npos);
    EXPECT_NE(r.headers.find("ETag:"), std::string::npos);
    auto u = GET("/static/theme.js");
    EXPECT_NE(u.headers.find("no-cache"), std::string::npos);
}

TEST_F(IntegrationTest, StaticEtagGives304) {
    auto r = GET("/static/theme.css");
    size_t p = r.headers.find("ETag: ");
    ASSERT_NE(p, std::string::npos);
    std::string etag = r.headers.substr(p + 6, r.headers.find("\r\n", p) - p - 6);
    auto again = http("GET", "/static/theme.css", "", "", false, "", false, {"If-None-Match: " + etag});
    EXPECT_EQ(again.code, 304);
}

TEST_F(IntegrationTest, StaticTextIsGzippedImagesAreNot) {
    auto css = http("GET", "/static/theme.css", "", "", false, "", false, {"Accept-Encoding: gzip"});
    EXPECT_NE(css.headers.find("Content-Encoding: gzip"), std::string::npos);
    auto png = http("GET", "/static/icon-192.png", "", "", false, "", false, {"Accept-Encoding: gzip"});
    EXPECT_EQ(png.headers.find("Content-Encoding: gzip"), std::string::npos);
}

TEST_F(IntegrationTest, StaticRejectsTraversal) {
    EXPECT_EQ(GET("/static/../CMakeLists.txt").code, 404);
    EXPECT_EQ(GET("/static/%2e%2e/CMakeLists.txt").code, 404);
    EXPECT_EQ(GET("/static/nope.css").code, 404);
}

// /healthz for Docker's HEALTHCHECK: public, plain, no details; and
// `lug_manager --healthcheck` reads it.
TEST_F(IntegrationTest, HealthCheck) {
    auto r = GET("/healthz");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.body, "ok\n");
    setenv("LUG_PORT", std::to_string(port).c_str(), 1);
    EXPECT_EQ(run_healthcheck(), 0);
    setenv("LUG_PORT", "1", 1);          // nothing listening there
    EXPECT_EQ(run_healthcheck(), 1);
    unsetenv("LUG_PORT");
}

// The logo, and the About LUG Manager page: version, GitHub, credits.
TEST_F(IntegrationTest, LogoAboutAndCredit) {
    EXPECT_EQ(GET("/favicon.ico").code, 200);
    auto svg = GET("/static/logo.svg");
    EXPECT_EQ(svg.code, 200);
    expect_contains(svg, "<svg");
    auto login = GET("/login");
    expect_contains(login, "/static/logo.svg");
    expect_contains(login, "github.com/ArkLUG/lug-manager");
    expect_not_contains(login, "Alexandria");                      // credit lives on the About page
    EXPECT_NE(GET("/about-lug-manager").code, 200);                 // signed in only
    auto about = GET("/about-lug-manager", member_token);
    EXPECT_EQ(about.code, 200);
    expect_contains(about, "Version ");
    expect_contains(about, "github.com");
    expect_contains(about, "Logo Concept by Brigs");
    expect_contains(about, "Final Design by Alexandria B (ROCLUG)");
    expect_contains(about, "trademark of the LEGO Group");
    expect_contains(GET("/help", member_token), "About LUG Manager");
    expect_contains(GET("/dashboard", member_token), "/static/logo.svg");
}
