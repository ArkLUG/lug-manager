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
