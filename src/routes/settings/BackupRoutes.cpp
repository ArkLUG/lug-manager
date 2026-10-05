#include "routes/settings/BackupRoutes.hpp"
#include "utils/text/Plural.hpp"
#include "utils/web/ParseId.hpp"
#include <crow/mustache.h>
#include <fstream>
#include <sstream>

namespace {

std::string human_size(std::uintmax_t b) {
    char buf[32];
    if (b >= 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
    else                  std::snprintf(buf, sizeof(buf), "%.0f KB", b / 1024.0);
    return buf;
}

std::string render_page(BackupService& backups, SettingsRepository& settings, const std::string& flash) {
    crow::mustache::context ctx;
    crow::json::wvalue arr = crow::json::wvalue::list();
    auto list = backups.list();
    for (size_t i = 0; i < list.size(); ++i) {
        arr[i]["name"] = list[i].name;
        arr[i]["when"] = list[i].when;
        arr[i]["size"] = human_size(list[i].bytes);
    }
    ctx["backups"]     = std::move(arr);
    ctx["has_backups"] = !list.empty();
    ctx["keep"]        = settings.get("backup_keep", "14");
    ctx["enabled"]     = settings.get("backup_enabled", "1") == "1";
    if (!flash.empty()) ctx["flash"] = flash;
    auto up = backups.upload_stats();
    ctx["upload_files"] = up.files;
    ctx["upload_files_text"] = count_of(up.files, "file", "files");
    ctx["upload_size"]  = human_size(up.bytes);
    ctx["has_uploads"]  = up.files > 0;
    return crow::mustache::load("settings/_backups.html").render(ctx).dump();
}

} // namespace

void register_backup_routes(LugApp& app, std::shared_ptr<BackupService> backups,
                            SettingsRepository& settings, AuditService& audit) {

    CROW_ROUTE(app, "/settings/backups")([&app, backups, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return html_page(req, app, render_page(*backups, settings, ""), "Backups", "active_backups");
    });

    // POST /settings/backups - "Back up now"
    CROW_ROUTE(app, "/settings/backups").methods("POST"_method)(
        [&app, backups, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        std::string flash;
        try {
            std::string name = backups->create();
            int keep = static_cast<int>(parse_id(settings.get("backup_keep", "14")));
            backups->prune(keep);
            backups->mirror_uploads(keep);
            audit.log(req, app, "settings.backup", "settings", 0, "", "Created backup " + name);
            flash = "Backup " + name + " created.";
        } catch (const std::exception& e) {
            res.code = 500;
            flash = std::string("Backup failed: ") + e.what();
        }
        res.write(render_page(*backups, settings, flash));
        return res;
    });

    // POST /settings/backups/schedule - enable/disable daily backups, retention
    CROW_ROUTE(app, "/settings/backups/schedule").methods("POST"_method)(
        [&app, backups, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto params = crow::query_string("?" + req.body);
        const char* en = params.get("backup_enabled");
        int keep = static_cast<int>(parse_id(params.get("backup_keep") ? params.get("backup_keep") : ""));
        if (keep < 1) keep = 1;
        if (keep > 365) keep = 365;
        settings.set("backup_enabled", en && std::string(en) == "1" ? "1" : "0");
        settings.set("backup_keep", std::to_string(keep));
        audit.log(req, app, "settings.update", "settings", 0, "", "Backup schedule updated");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_page(*backups, settings, "Saved."));
        return res;
    });

    // GET /settings/backups/photos.zip - every uploaded photo/receipt, built
    // fresh and streamed from disk (can be large).
    CROW_ROUTE(app, "/settings/backups/photos.zip")([&app, backups, &audit](const crow::request& req, crow::response& res) {
        if (!require_auth(req, res, app, "admin")) { res.end(); return; }
        std::string path;
        try { path = backups->build_upload_archive(); } catch (const std::exception& e) {
            res.code = 500; res.write(std::string("Couldn't build the archive: ") + e.what()); res.end(); return;
        }
        if (path.empty()) { res.code = 404; res.write("No uploaded files yet."); res.end(); return; }
        audit.log(req, app, "settings.backup_download", "settings", 0, "", "Downloaded photos archive");
        res.set_static_file_info_unsafe(path);
        res.set_header("Content-Type", "application/zip");
        res.add_header("Content-Disposition", "attachment; filename=\"lug-photos.zip\"");
        res.add_header("Cache-Control", "no-store");
        res.end();
    });

    // GET /settings/backups/<name> - download (admin). Name is validated
    // against the exact backup filename pattern, so no path tricks.
    CROW_ROUTE(app, "/settings/backups/<string>")([&app, backups, &audit](const crow::request& req,
                                                                          crow::response& res, const std::string& name) {
        if (!require_auth(req, res, app, "admin")) { res.end(); return; }
        std::string path = backups->path_of(name);
        if (path.empty()) { res.code = 404; res.end(); return; }
        audit.log(req, app, "settings.backup_download", "settings", 0, "", "Downloaded backup " + name);
        res.set_static_file_info_unsafe(path);   // streamed from disk
        res.set_header("Content-Type", "application/octet-stream");
        res.add_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
        res.add_header("Cache-Control", "no-store");
        res.end();
    });
}
