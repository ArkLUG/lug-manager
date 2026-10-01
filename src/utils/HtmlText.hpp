#pragma once
// Plain text from HTML or Markdown: for list excerpts and meta descriptions.
#include "utils/MarkdownRenderer.hpp"
#include "utils/Utf8.hpp"
#include <cstring>
#include <set>
#include <string>
#include <utility>

// Text of rendered HTML; block tags become spaces, basic entities decoded.
inline std::string html_to_text(const std::string& html) {
    static const std::set<std::string> blocks{"p", "h1", "h2", "h3", "h4", "h5", "h6", "li", "ul", "ol",
                                              "blockquote", "br", "hr", "img", "table", "tr", "td", "th", "pre"};
    std::string out, tag;
    bool in_tag = false, space = false;
    for (char c : html) {
        if (c == '<') { in_tag = true; tag.clear(); continue; }
        if (in_tag) {
            if (c == '>') {
                in_tag = false;
                std::string name = tag.empty() ? "" : tag.substr(tag[0] == '/' ? 1 : 0);
                name = name.substr(0, name.find_first_of(" /"));
                if (blocks.count(name)) space = true;
            } else {
                tag += c;
            }
            continue;
        }
        if (c == '\n' || c == '\t' || c == ' ') { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    for (auto [from, to] : {std::pair<const char*, const char*>{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
                            {"&#39;", "'"}, {"&amp;", "&"}})
        for (size_t p = 0; (p = out.find(from, p)) != std::string::npos; ++p) out.replace(p, std::strlen(from), to);
    return out;
}

// The first `max_chars` characters of a Markdown text, as plain text.
inline std::string markdown_excerpt(const std::string& markdown, size_t max_chars = 160) {
    if (markdown.empty()) return "";
    std::string text = html_to_text(render_markdown(markdown.substr(0, 4000)));
    std::string cut = utf8_truncate(text, max_chars);
    return cut.size() < text.size() ? cut + "…" : cut;
}
