#include "utils/MarkdownRenderer.hpp"
#include <md4c-html.h>

static void md_output(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(text, size);
}

std::string render_markdown(const std::string& markdown) {
    std::string html;
    html.reserve(markdown.size() * 2);

    unsigned flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    int rc = md_html(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()),
                     md_output, &html, flags, 0);
    if (rc != 0) return markdown; // fallback to raw text on error
    return html;
}
