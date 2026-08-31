#include "utils/MarkdownRenderer.hpp"
#include <md4c-html.h>

static void md_output(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(text, size);
}

std::string render_markdown(const std::string& markdown) {
    std::string html;
    html.reserve(markdown.size() * 2);

    // MD_FLAG_NOHTML: notes/description fields are user-supplied (chapter
    // lead/event manager, not just admin) and rendered to any viewer of the
    // event/meeting detail page - without this, md4c passes raw inline/block
    // HTML in the markdown source straight through unescaped (stored XSS).
    // With it, would-be-raw-HTML is treated as literal text and escaped like
    // any other text node, which is md4c's standard safe configuration for
    // untrusted input.
    unsigned flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_NOHTML;
    int rc = md_html(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()),
                     md_output, &html, flags, 0);
    if (rc != 0) return markdown; // fallback to raw text on error
    return html;
}
