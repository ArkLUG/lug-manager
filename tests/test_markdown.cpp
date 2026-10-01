#include <gtest/gtest.h>
#include "utils/MarkdownRenderer.hpp"

TEST(MarkdownRenderer, BasicParagraph) {
    auto html = render_markdown("Hello world");
    EXPECT_NE(html.find("<p>Hello world</p>"), std::string::npos);
}

TEST(MarkdownRenderer, Heading) {
    auto html = render_markdown("# Title");
    EXPECT_NE(html.find("<h1>Title</h1>"), std::string::npos);
}

TEST(MarkdownRenderer, Bold) {
    auto html = render_markdown("**bold text**");
    EXPECT_NE(html.find("<strong>bold text</strong>"), std::string::npos);
}

TEST(MarkdownRenderer, Italic) {
    auto html = render_markdown("*italic text*");
    EXPECT_NE(html.find("<em>italic text</em>"), std::string::npos);
}

TEST(MarkdownRenderer, UnorderedList) {
    auto html = render_markdown("- item 1\n- item 2");
    EXPECT_NE(html.find("<ul>"), std::string::npos);
    EXPECT_NE(html.find("<li>item 1</li>"), std::string::npos);
    EXPECT_NE(html.find("<li>item 2</li>"), std::string::npos);
}

TEST(MarkdownRenderer, Link) {
    auto html = render_markdown("[click](http://example.com)");
    EXPECT_NE(html.find("<a href=\"http://example.com\">click</a>"), std::string::npos);
}

TEST(MarkdownRenderer, Strikethrough) {
    auto html = render_markdown("~~deleted~~");
    EXPECT_NE(html.find("<del>deleted</del>"), std::string::npos);
}

TEST(MarkdownRenderer, Table) {
    std::string md = "| A | B |\n|---|---|\n| 1 | 2 |";
    auto html = render_markdown(md);
    EXPECT_NE(html.find("<table>"), std::string::npos);
    EXPECT_NE(html.find("<td>1</td>"), std::string::npos);
}

TEST(MarkdownRenderer, EmptyInput) {
    auto html = render_markdown("");
    EXPECT_TRUE(html.empty());
}

TEST(MarkdownRenderer, MultipleBlocks) {
    std::string md = "# Report\n\nSome text.\n\n- item\n";
    auto html = render_markdown(md);
    EXPECT_NE(html.find("<h1>"), std::string::npos);
    EXPECT_NE(html.find("<p>"), std::string::npos);
    EXPECT_NE(html.find("<li>"), std::string::npos);
}

// Security regression: notes/description fields (event/meeting reports) are
// written by chapter leads/event managers, not just admins, and rendered
// to any viewer of the detail page via {{{notes_html}}} (unescaped, since
// it's meant to contain real HTML from markdown). Raw HTML embedded in the
// markdown source must never pass through unescaped - MD_FLAG_NOHTML makes
// md4c treat it as literal text (escaped like any other text node) instead.
TEST(MarkdownRenderer, RawHtmlBlockIsEscapedNotPassedThrough) {
    auto html = render_markdown("<script>alert(1)</script>");
    EXPECT_EQ(html.find("<script>"), std::string::npos);
    EXPECT_NE(html.find("&lt;script&gt;"), std::string::npos);
}

TEST(MarkdownRenderer, RawHtmlInlineSpanIsEscapedNotPassedThrough) {
    auto html = render_markdown("before <img src=x onerror=alert(1)> after");
    EXPECT_EQ(html.find("<img"), std::string::npos);
    EXPECT_NE(html.find("&lt;img"), std::string::npos);
}

// md4c passes link destinations through verbatim - script-capable schemes
// must not survive as clickable links (notes render unescaped).
TEST(MarkdownRenderer, JavascriptLinkIsNeutralized) {
    auto html = render_markdown("[click](javascript:alert(1)) ![i](JaVaScRiPt:alert(2)) [d](data:text/html,x)");
    EXPECT_EQ(html.find("javascript:"), std::string::npos);
    EXPECT_EQ(html.find("JaVaScRiPt:"), std::string::npos);
    EXPECT_EQ(html.find("data:"), std::string::npos);
    EXPECT_NE(html.find("href=\"#\""), std::string::npos);
}

TEST(MarkdownRenderer, SafeLinksAreKept) {
    auto html = render_markdown("[a](https://example.com/x?y=1) [b](/events/3) [c](mailto:a@b.c) [d](#top)");
    EXPECT_NE(html.find("href=\"https://example.com/x?y=1\""), std::string::npos);
    EXPECT_NE(html.find("href=\"/events/3\""), std::string::npos);
    EXPECT_NE(html.find("href=\"mailto:a@b.c\""), std::string::npos);
    EXPECT_NE(html.find("href=\"#top\""), std::string::npos);
}

// Character references and control characters in the scheme must not sneak a
// script link past the check (browsers decode &#x61; and drop tabs/newlines).
TEST(MarkdownRenderer, EncodedJavascriptLinkIsNeutralized) {
    for (const char* md : {"[x](jav&#x61;script:alert(1))", "[x](&#106;avascript:alert(1))",
                           "[x](javas&#9;cript:alert(1))", "[x](<java\tscript:alert(1)>)",
                           "[x](&#x6A;&#x61;vascript:alert(1))", "[x](javascript&colon;alert(1))"}) {
        auto html = render_markdown(md);
        std::string lower;
        for (char c : html) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        EXPECT_EQ(lower.find("script:"), std::string::npos) << md << " -> " << html;
        EXPECT_EQ(lower.find("script&"), std::string::npos) << md << " -> " << html;
        EXPECT_EQ(lower.find("&#"), std::string::npos) << md << " -> " << html;
    }
}
