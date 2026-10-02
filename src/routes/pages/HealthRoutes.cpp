#include "routes/pages/HealthRoutes.hpp"
#include <curl/curl.h>
#include <cstdlib>
#include <string>

void register_health_routes(LugApp& app, SqliteDatabase& db) {
    CROW_ROUTE(app, "/healthz")([&db] {
        crow::response res;
        res.add_header("Content-Type", "text/plain; charset=utf-8");
        res.add_header("Cache-Control", "no-store");
        try {
            auto st = db.prepare("SELECT 1");
            res.code = st.step() ? 200 : 503;
        } catch (...) {
            res.code = 503;
        }
        res.write(res.code == 200 ? "ok\n" : "database unavailable\n");
        return res;
    });
}

int run_healthcheck() {
    const char* port = std::getenv("LUG_PORT");
    const std::string url = std::string("http://127.0.0.1:") + (port && *port ? port : "8080") + "/healthz";
    CURL* curl = curl_easy_init();
    if (!curl) return 1;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char*, size_t s, size_t n, void*) { return s * n; });
    long code = 0;
    bool ok = curl_easy_perform(curl) == CURLE_OK &&
              curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK && code == 200;
    curl_easy_cleanup(curl);
    return ok ? 0 : 1;
}
