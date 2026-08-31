// Unit tests for the HTML/JSON escape helpers added as part of the stored/
// reflected XSS fixes across every hand-built <option>/<span>/hx-vals HTML
// fragment in the route handlers (see HtmlEscape.hpp/JsonEscape.hpp).
#include <gtest/gtest.h>
#include "utils/HtmlEscape.hpp"
#include "utils/JsonEscape.hpp"

TEST(HtmlEscape, EscapesAllFiveSpecialCharacters) {
    EXPECT_EQ(html_escape("&"), "&amp;");
    EXPECT_EQ(html_escape("<"), "&lt;");
    EXPECT_EQ(html_escape(">"), "&gt;");
    EXPECT_EQ(html_escape("\""), "&quot;");
    EXPECT_EQ(html_escape("'"), "&#39;");
}

TEST(HtmlEscape, LeavesPlainTextUnchanged) {
    EXPECT_EQ(html_escape("Regular Display Name"), "Regular Display Name");
}

TEST(HtmlEscape, NeutralizesScriptTag) {
    std::string out = html_escape("<script>alert(1)</script>");
    EXPECT_EQ(out.find("<script>"), std::string::npos);
    EXPECT_EQ(out, "&lt;script&gt;alert(1)&lt;/script&gt;");
}

TEST(HtmlEscape, NeutralizesAttributeBreakoutViaQuote) {
    // The classic "close the attribute early" payload used against a
    // src="..." or value="..." context.
    std::string out = html_escape("x\" onerror=\"alert(1)");
    EXPECT_EQ(out.find('"'), std::string::npos);
}

TEST(HtmlEscape, NeutralizesSingleQuoteAttributeBreakout) {
    // Same idea, for the single-quoted hx-vals='...' attributes this app
    // uses - a raw "'" would close the attribute early regardless of what
    // else is escaped.
    std::string out = html_escape("x' onmouseover='alert(1)");
    EXPECT_EQ(out.find('\''), std::string::npos);
}

TEST(JsonEscape, EscapesQuoteAndBackslash) {
    EXPECT_EQ(json_escape("\""), "\\\"");
    EXPECT_EQ(json_escape("\\"), "\\\\");
}

TEST(JsonEscape, EscapesControlCharacters) {
    EXPECT_EQ(json_escape("\n"), "\\n");
    EXPECT_EQ(json_escape("\r"), "\\r");
    EXPECT_EQ(json_escape("\t"), "\\t");
}

TEST(JsonEscape, LeavesPlainTextUnchanged) {
    EXPECT_EQ(json_escape("Regular Display Name"), "Regular Display Name");
}

// The composite pattern used for hx-vals='{"...":"..."}' attributes
// (SettingsRoutes.cpp's sync-revert table): json_escape for JSON-string
// correctness, then html_escape so an embedded "'" can't close the
// surrounding single-quoted HTML attribute early. Neither escape alone is
// sufficient for this nested context.
TEST(JsonEscape, ComposesWithHtmlEscapeForAttributeContext) {
    std::string malicious = "x\"}' onmouseover='alert(1)";
    std::string safe = html_escape(json_escape(malicious));
    // No raw single quote survives (would close the HTML attribute).
    EXPECT_EQ(safe.find('\''), std::string::npos);
    // No raw double quote survives unescaped either (would break the JSON
    // string before html_escape's &quot; conversion, if applied in the
    // wrong order).
    EXPECT_EQ(safe.find("\"}"), std::string::npos);
}
