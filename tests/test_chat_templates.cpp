// Message templates: syntax and the built-in defaults.
#include <gtest/gtest.h>
#include "chat/Templates.hpp"

using chat::render;

TEST(ChatTemplates, Placeholders) {
    EXPECT_EQ(render("Hi {name}!", {{"name", "Ann"}}), "Hi Ann!");
    EXPECT_EQ(render("{a}{b}", {{"a", "1"}, {"b", "2"}}), "12");
    EXPECT_EQ(render("Hi {nobody}", {}), "");                               // only placeholder, empty -> line gone
    EXPECT_EQ(render("{{literal}} and }}", {}), "{literal} and }");
    EXPECT_EQ(render("{Not A Placeholder}", {}), "{Not A Placeholder}");
}

TEST(ChatTemplates, EmptyLinesAndOptionalParts) {
    chat::Values v{{"title", "Show"}, {"location", ""}};
    EXPECT_EQ(render("**{title}**\nWhere: {location}\nEnd", v), "**Show**\nEnd");
    EXPECT_EQ(render("{title}[[ at {location}]]!", v), "Show!");
    v["location"] = "Hall";
    EXPECT_EQ(render("{title}[[ at {location}]]!", v), "Show at Hall!");
    // A line with text and only-empty placeholders is still dropped; mixed lines stay
    chat::Values w{{"a", "x"}, {"b", ""}};
    EXPECT_EQ(render("A {a} {b}\nB {b}", w), "A x");
    // Optional groups may span lines; tidy blank lines
    EXPECT_EQ(render("Top[[\n\n## Notes\n{n}]]", {{"n", ""}}), "Top");
    EXPECT_EQ(render("Top[[\n\n## Notes\n{n}]]", {{"n", "Hi"}}), "Top\n\n## Notes\nHi");
    EXPECT_EQ(render("\n\n{a}\n\n\n\nb\n\n", w), "x\n\nb");
    EXPECT_EQ(render("line   \nnext", {}), "line\nnext");                  // trailing spaces trimmed
}

TEST(ChatTemplates, DefaultsAreComplete) {
    std::set<std::string> keys;
    for (const auto& d : chat::all_templates()) {
        EXPECT_TRUE(keys.insert(d.key).second) << "duplicate " << d.key;
        EXPECT_TRUE(chat::unknown_placeholders(d, d.default_body).empty()) << d.key;
        EXPECT_TRUE(chat::unknown_placeholders(d, d.default_subject).empty()) << d.key;
        for (const char* r : d.required)
            EXPECT_NE(std::string(d.default_body).find(std::string("{") + r + "}"), std::string::npos) << d.key << " " << r;
        std::string out = render(d.default_body, chat::sample_values(d));
        EXPECT_FALSE(out.empty()) << d.key;
        if (d.max_len) EXPECT_LE(out.size(), d.max_len) << d.key;
        EXPECT_EQ(out.find("[["), std::string::npos) << d.key;
        EXPECT_NE(d.label[0], '\0');
        EXPECT_NE(d.help[0], '\0');
    }
    EXPECT_GE(keys.size(), 24u);
    auto unknown = chat::unknown_placeholders(*chat::find_template("dm.waitlist"), "Hi {name}, {nope} {{x}}");
    ASSERT_EQ(unknown.size(), 1u);
    EXPECT_EQ(unknown[0], "nope");
}
