// Colour themes: each member's choice, the LUG default, and the pages that
// carry them (static/palettes.css does the colouring).
#include "integration_test_base.hpp"

TEST_F(IntegrationTest, PaletteDefaultsToClassic) {
    expect_contains(GET("/dashboard", member_token), "data-palette=\"classic\"");
    expect_contains(GET("/login"), "data-palette=\"classic\"");
    expect_contains(GET("/login"), "/static/palettes.css");
}

TEST_F(IntegrationTest, MemberPicksTheirOwnPalette) {
    EXPECT_EQ(POST("/account/palette", "palette=creator", member_token).code, 204);
    expect_contains(GET("/dashboard", member_token), "data-palette=\"creator\"");
    expect_contains(GET("/account", member_token), "value=\"creator\" checked");
    expect_contains(GET("/dashboard", admin_token), "data-palette=\"classic\"");   // only theirs
    POST("/account/palette", "palette=nonsense", member_token);                     // back to the LUG default
    expect_contains(GET("/dashboard", member_token), "data-palette=\"classic\"");
    EXPECT_NE(POST("/account/palette", "palette=creator").code, 204);               // signed in only
}

TEST_F(IntegrationTest, LugDefaultPalette) {
    EXPECT_EQ(POST("/settings/branding/palette", "default_palette=community", member_token).code, 403);
    EXPECT_EQ(POST("/settings/branding/palette", "default_palette=bogus", admin_token).code, 400);
    auto r = POST("/settings/branding/palette", "default_palette=fancolab", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Saved.");
    expect_contains(GET("/dashboard", member_token), "data-palette=\"fancolab\"");   // hasn't picked one
    expect_contains(GET("/login"), "data-palette=\"fancolab\"");                     // public pages too
    POST("/account/palette", "palette=classic", member_token);
    expect_contains(GET("/dashboard", member_token), "data-palette=\"classic\"");    // their choice wins
    expect_contains(GET("/settings/branding", admin_token), "LEGO Fan CoLab");
}
