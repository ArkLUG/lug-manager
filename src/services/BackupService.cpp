#include "services/BackupService.hpp"
#include <algorithm>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

bool BackupService::valid_name(const std::string& name) {
    static const std::regex re(R"(lug-\d{8}-\d{6}\.db)");
    return std::regex_match(name, re);
}

std::string BackupService::create() {
    fs::create_directories(dir_);
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "lug-%Y%m%d-%H%M%S.db", &tm);
    std::string name = buf;
    std::string path = dir_ + "/" + name;
    std::error_code ec;
    fs::remove(path, ec); // VACUUM INTO refuses to overwrite
    // Path is built from our own timestamped name only; quote-escape anyway.
    std::string quoted;
    for (char c : path) { quoted += c; if (c == '\'') quoted += '\''; }
    // Snapshot through a separate connection when the DB is a real file: in
    // WAL mode its read doesn't block the app's shared connection, so pages
    // keep working while a large backup runs. (:memory: DBs - tests - can only
    // be reached through the original connection.)
    if (!db_.path().empty() && db_.path() != ":memory:") {
        SqliteDatabase snap(db_.path());
        snap.execute("VACUUM INTO '" + quoted + "'");
    } else {
        db_.execute("VACUUM INTO '" + quoted + "'");
    }
    return name;
}

std::vector<BackupService::Entry> BackupService::list() const {
    std::vector<Entry> out;
    std::error_code ec;
    if (!fs::exists(dir_, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        std::string name = e.path().filename().string();
        if (!e.is_regular_file() || !valid_name(name)) continue;
        Entry en;
        en.name = name;
        en.bytes = e.file_size(ec);
        // lug-YYYYMMDD-HHMMSS.db -> YYYY-MM-DD HH:MM:SS UTC
        en.when = name.substr(4, 4) + "-" + name.substr(8, 2) + "-" + name.substr(10, 2) + " " +
                  name.substr(13, 2) + ":" + name.substr(15, 2) + ":" + name.substr(17, 2) + " UTC";
        out.push_back(std::move(en));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.name > b.name; });
    return out;
}

void BackupService::prune(int keep) {
    if (keep < 1) keep = 1;
    auto all = list();
    std::error_code ec;
    for (size_t i = static_cast<size_t>(keep); i < all.size(); ++i)
        fs::remove(dir_ + "/" + all[i].name, ec);
}

bool BackupService::create_if_due(int hours, int keep) {
    auto all = list();
    if (!all.empty()) {
        std::error_code ec;
        auto t = fs::last_write_time(dir_ + "/" + all.front().name, ec);
        if (!ec) {
            auto age = fs::file_time_type::clock::now() - t;
            if (age < std::chrono::hours(hours)) return false;
        }
    }
    create();
    prune(keep);
    return true;
}

std::string BackupService::path_of(const std::string& name) const {
    if (!valid_name(name)) return "";
    std::string p = dir_ + "/" + name;
    std::error_code ec;
    return fs::is_regular_file(p, ec) ? p : "";
}
