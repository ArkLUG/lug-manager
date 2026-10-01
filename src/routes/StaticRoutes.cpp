#include "routes/StaticRoutes.hpp"
#include "utils/Crypto.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

struct Asset {
    std::string body;
    std::string type;
    std::string etag;
    bool        compressible = true;
    fs::file_time_type mtime;
};

std::string content_type_for(const std::string& ext, bool& compressible) {
    static const std::unordered_map<std::string, std::pair<const char*, bool>> types = {
        {".css", {"text/css; charset=utf-8", true}},
        {".js", {"text/javascript; charset=utf-8", true}},
        {".html", {"text/html; charset=utf-8", true}},
        {".json", {"application/json", true}},
        {".webmanifest", {"application/manifest+json", true}},
        {".yaml", {"application/yaml", true}},
        {".svg", {"image/svg+xml", true}},
        {".txt", {"text/plain; charset=utf-8", true}},
        {".png", {"image/png", false}},
        {".jpg", {"image/jpeg", false}},
        {".ico", {"image/x-icon", false}},
        {".woff2", {"font/woff2", false}},
    };
    auto it = types.find(ext);
    if (it == types.end()) { compressible = false; return "application/octet-stream"; }
    compressible = it->second.second;
    return it->second.first;
}

std::mutex g_mutex;
std::unordered_map<std::string, Asset> g_cache;

// Returns nullptr if the path isn't a regular file inside src/static.
const Asset* load(const std::string& rel) {
    if (rel.empty() || rel.find("..") != std::string::npos || rel[0] == '/' ||
        rel.find('\\') != std::string::npos || rel.find('\0') != std::string::npos)
        return nullptr;
    fs::path p = fs::path("src/static") / rel;
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return nullptr;
    auto mtime = fs::last_write_time(p, ec);

    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_cache.find(rel);
    if (it != g_cache.end() && it->second.mtime == mtime) return &it->second;

    std::ifstream in(p, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    Asset a;
    a.body  = buf.str();
    a.type  = content_type_for(p.extension().string(), a.compressible);
    a.etag  = "\"" + sha256_hex(a.body).substr(0, 20) + "\"";
    a.mtime = mtime;
    return &(g_cache[rel] = std::move(a));
}

} // namespace

void register_static_routes(LugApp& app) {
    // Takes crow::response& (not a returned value): Crow's response move
    // assignment drops the `compressed` flag, so returning a response could
    // never opt images out of gzip.
    CROW_ROUTE(app, "/static/<path>")([](const crow::request& req, crow::response& res, const std::string& rel) {
        const Asset* a = load(rel);
        if (!a) { res.code = 404; res.end(); return; }
        if (a->type.rfind("text/html", 0) == 0) {
            // HTML gets a per-request CSP nonce stamped into its inline
            // scripts, so it must never be served from cache (or a 304).
            res.add_header("Cache-Control", "no-store");
            res.add_header("Content-Type", a->type);
            res.write(a->body);
            res.end();
            return;
        }
        res.add_header("ETag", a->etag);
        // Versioned URLs (?v=<asset_version>) never change content: cache for
        // a year. Unversioned ones revalidate (cheap 304 via ETag).
        res.add_header("Cache-Control", req.url_params.get("v") ? "public, max-age=31536000, immutable"
                                                                 : "public, no-cache");
        if (req.get_header_value("If-None-Match") == a->etag) {
            res.code = 304;
            res.end();
            return;
        }
        res.add_header("Content-Type", a->type);
        res.compressed = a->compressible; // images are already compressed
        res.write(a->body);
        res.end();
    });
}
