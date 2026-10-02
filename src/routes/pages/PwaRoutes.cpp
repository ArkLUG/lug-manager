#include "routes/pages/PwaRoutes.hpp"
#include <fstream>
#include <sstream>

namespace {
crow::response serve(const std::string& file, const std::string& type) {
    crow::response res;
    std::ifstream in("src/static/" + file, std::ios::binary);
    if (!in) { res.code = 404; return res; }
    std::ostringstream buf;
    buf << in.rdbuf();
    res.add_header("Content-Type", type);
    res.add_header("Cache-Control", "no-cache"); // let SW/manifest updates through promptly
    res.write(buf.str());
    return res;
}
}

void register_pwa_routes(LugApp& app) {
    CROW_ROUTE(app, "/sw.js")([] { return serve("sw.js", "text/javascript; charset=utf-8"); });
    // Browsers ask for /favicon.ico on their own (e.g. for JSON or PDF tabs): the logo.
    CROW_ROUTE(app, "/favicon.ico")([] { return serve("favicon-32.png", "image/png"); });
    CROW_ROUTE(app, "/manifest.webmanifest")([] {
        return serve("manifest.webmanifest", "application/manifest+json");
    });
}
