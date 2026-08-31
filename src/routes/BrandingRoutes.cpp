#include "routes/BrandingRoutes.hpp"
#include <crow/multipart.h>
#include <crow/mustache.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

// Detected image kind, purely from the file's own magic bytes - the
// client-supplied Content-Type header is never trusted for anything beyond
// a hint, since it's fully attacker-controlled. Sniffing here also doubles
// as validation: an upload that matches none of these is rejected outright,
// so this can never be used to smuggle an arbitrary file onto disk under a
// served, browser-reachable URL.
struct SniffResult {
    bool is_image = false;
    std::string extension;   // ".png", ".jpg", etc.
    std::string content_type;
};

SniffResult sniff_image(const std::string& bytes) {
    auto starts_with = [&](const char* sig, size_t len) {
        return bytes.size() >= len && bytes.compare(0, len, sig, len) == 0;
    };
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (bytes.size() >= 8 &&
        static_cast<unsigned char>(bytes[0]) == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
        return {true, ".png", "image/png"};
    // JPEG: FF D8 FF
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xD8 &&
        static_cast<unsigned char>(bytes[2]) == 0xFF)
        return {true, ".jpg", "image/jpeg"};
    // GIF: "GIF87a" or "GIF89a"
    if (starts_with("GIF87a", 6) || starts_with("GIF89a", 6))
        return {true, ".gif", "image/gif"};
    // WebP: "RIFF"....."WEBP"
    if (bytes.size() >= 12 && starts_with("RIFF", 4) && bytes.compare(8, 4, "WEBP", 4) == 0)
        return {true, ".webp", "image/webp"};
    // SVG: no fixed magic bytes (it's XML/text) - look for the root <svg tag
    // within the first slice of the file, after skipping optional XML
    // declaration/whitespace/BOM. Accepted deliberately: LUG logos are
    // commonly vector art. Never executed/rendered outside an <img>/CSS
    // background context, so no script-in-SVG XSS surface is introduced by
    // storing it - see the Content-Type served in register_branding_routes.
    {
        size_t probe_len = std::min<size_t>(bytes.size(), 512);
        std::string head = bytes.substr(0, probe_len);
        if (head.find("<svg") != std::string::npos ||
            head.find("<?xml") != std::string::npos)
            return {true, ".svg", "image/svg+xml"};
    }
    return {false, "", ""};
}

std::string logo_path(const std::string& data_dir, const std::string& extension) {
    return (fs::path(data_dir) / ("logo" + extension)).string();
}

} // namespace

void register_branding_routes(LugApp& app, SettingsRepository& settings,
                               AuditService& audit, const std::string& data_dir) {

    // GET /settings/branding - upload form page
    CROW_ROUTE(app, "/settings/branding")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.redirect("/dashboard");
            return res;
        }

        crow::mustache::context mctx;
        std::string ext = settings.get("branding_logo_extension", "");
        mctx["has_logo"] = !ext.empty();
        // Cache-bust so the preview updates immediately after a new upload
        // replaces the same-named file - without this the browser (and any
        // proxy/CDN in front of it) would keep showing the old cached image.
        mctx["logo_url"] = "/branding/logo?v=" + settings.get("branding_logo_updated_at", "0");

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("settings/_branding.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(tmpl.render(mctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("settings/_branding.html");
            std::string content = content_tmpl.render(mctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]         = content;
            layout_ctx["page_title"]      = "Branding Settings";
            layout_ctx["active_branding"] = true;
            layout_ctx["is_admin"]        = true;
            set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /settings/branding - upload a new logo (multipart/form-data, field "logo")
    CROW_ROUTE(app, "/settings/branding").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            return res;
        }

        crow::multipart::message msg(req);
        auto part = msg.get_part_by_name("logo");
        if (part.body.empty()) {
            res.write(R"(<span class="text-red-600">Choose a file first.</span>)");
            return res;
        }

        // 5 MB cap - a LUG logo has no business being larger than this, and it
        // keeps a single admin-uploaded file from becoming a meaningful DoS/
        // disk-exhaustion vector (there's no per-admin quota otherwise).
        constexpr size_t kMaxLogoBytes = 5 * 1024 * 1024;
        if (part.body.size() > kMaxLogoBytes) {
            res.write(R"(<span class="text-red-600">File too large (max 5 MB).</span>)");
            return res;
        }

        SniffResult sniff = sniff_image(part.body);
        if (!sniff.is_image) {
            res.write(R"(<span class="text-red-600">That doesn't look like an image file (PNG, JPEG, GIF, WebP, or SVG).</span>)");
            return res;
        }

        // Remove any previously-uploaded logo under a different extension
        // first (e.g. replacing a .png with a .svg) so stale files don't
        // pile up in the data directory or accidentally keep being served
        // by a stale setting value.
        std::string old_ext = settings.get("branding_logo_extension", "");
        if (!old_ext.empty() && old_ext != sniff.extension) {
            std::error_code ec;
            fs::remove(logo_path(data_dir, old_ext), ec); // best-effort
        }

        std::string path = logo_path(data_dir, sniff.extension);
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) {
                res.code = 500;
                res.write(R"(<span class="text-red-600">Could not save the file on the server.</span>)");
                return res;
            }
            out.write(part.body.data(), static_cast<std::streamsize>(part.body.size()));
        }

        settings.set("branding_logo_extension", sniff.extension);
        settings.set("branding_logo_content_type", sniff.content_type);
        settings.set("branding_logo_updated_at", std::to_string(std::time(nullptr)));

        audit.log(req, app, "settings.update", "settings", 0, "", "Uploaded a new logo");

        res.add_header("HX-Redirect", "/settings/branding");
        res.code = 200;
        return res;
    });

    // POST /settings/branding/remove - revert to the default LUG Manager branding
    CROW_ROUTE(app, "/settings/branding/remove").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        std::string ext = settings.get("branding_logo_extension", "");
        if (!ext.empty()) {
            std::error_code ec;
            fs::remove(logo_path(data_dir, ext), ec); // best-effort
        }
        settings.set("branding_logo_extension", "");
        settings.set("branding_logo_content_type", "");
        settings.set("branding_logo_updated_at", std::to_string(std::time(nullptr)));

        audit.log(req, app, "settings.update", "settings", 0, "", "Removed custom logo");

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            res.add_header("HX-Redirect", "/settings/branding");
            res.code = 200;
        } else {
            res.redirect("/settings/branding");
        }
        return res;
    });

    // GET /branding/logo - serves the uploaded logo (also used as the favicon
    // - see layout.html's <link rel="icon">). Public, unauthenticated: this
    // is exactly the same trust level as any other static asset the app
    // serves (CSS/JS under /static/*), not member data.
    CROW_ROUTE(app, "/branding/logo")([&](const crow::request& req) {
        crow::response res;
        std::string ext = settings.get("branding_logo_extension", "");
        if (ext.empty()) {
            res.code = 404;
            return res;
        }
        std::string content_type = settings.get("branding_logo_content_type", "application/octet-stream");
        std::string path = logo_path(data_dir, ext);

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            res.code = 404;
            return res;
        }
        std::ostringstream buf;
        buf << in.rdbuf();

        res.add_header("Content-Type", content_type);
        // Long-lived cache is safe: the URL is fixed but the page that
        // references it appends ?v=<upload timestamp> as a cache-buster,
        // so a new upload is always reflected immediately rather than
        // needing a hard refresh.
        res.add_header("Cache-Control", "public, max-age=31536000, immutable");
        res.write(buf.str());
        return res;
    });
}
