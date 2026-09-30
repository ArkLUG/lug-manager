#include "utils/MarkdownRenderer.hpp"
#include "utils/HtmlEscape.hpp"
#include <md4c-html.h>
#include <algorithm>
#include <cctype>

static void md_output(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(text, size);
}

// True if a rendered href/src value is safe to leave clickable: relative
// URLs, fragments, and http/https/mailto. md4c passes link destinations
// through verbatim, so [x](javascript:alert(1)) would otherwise become a
// clickable script link. Checked as an allowlist on the raw attribute text:
// anything with an entity or other oddity in the scheme fails the match.
static bool is_safe_url(const std::string& url) {
    size_t stop = url.find_first_of(":/?#");
    if (stop == std::string::npos || url[stop] != ':') return true; // no scheme -> relative
    std::string scheme = url.substr(0, stop);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return scheme == "http" || scheme == "https" || scheme == "mailto";
}

// Rewrites unsafe href="..." / src="..." values in md4c's output to "#".
static std::string sanitize_urls(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    size_t pos = 0;
    while (pos < html.size()) {
        size_t href = html.find(" href=\"", pos);
        size_t src  = html.find(" src=\"", pos);
        size_t attr = std::min(href, src);
        if (attr == std::string::npos) break;
        size_t val_start = html.find('"', attr) + 1;
        size_t val_end   = html.find('"', val_start);
        if (val_end == std::string::npos) break;
        out.append(html, pos, val_start - pos);
        std::string val = html.substr(val_start, val_end - val_start);
        out += is_safe_url(val) ? val : "#";
        pos = val_end;
    }
    out.append(html, pos, std::string::npos);
    return out;
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
    // Fallback is escaped: callers insert the result unescaped ({{{notes_html}}}).
    if (rc != 0) return html_escape(markdown);
    return sanitize_urls(html);
}
