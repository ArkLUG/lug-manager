// /static serving: caching, ETags, compression, path safety.
#include "integration_test_base.hpp"

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
