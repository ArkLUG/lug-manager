#pragma once
#include "db/SqliteDatabase.hpp"
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// Consistent SQLite snapshots (VACUUM INTO) under <data_dir>/backups, named
// lug-YYYYMMDD-HHMMSS.db, keeping the newest `keep` files.
class BackupService {
public:
    BackupService(SqliteDatabase& db, std::string data_dir)
        : db_(db), dir_(data_dir + "/backups"), uploads_(data_dir + "/uploads") {}

    struct Entry { std::string name; std::uintmax_t bytes = 0; std::string when; };

    std::string create();                       // returns file name
    std::vector<Entry> list() const;            // newest first
    void prune(int keep);
    // Creates a backup if the newest is older than `hours` (or none exist).
    bool create_if_due(int hours, int keep);
    // Absolute path of a listed backup, or "" if `name` isn't one (path-safe).
    std::string path_of(const std::string& name) const;
    static bool valid_name(const std::string& name);

    // Uploaded photos/receipts aren't in the DB snapshot. Each backup copies
    // new uploads into <data_dir>/backups/uploads; a copy whose original was
    // deleted is kept for `keep_days` (so a mistaken delete can be undone),
    // then removed. Returns the number of files copied.
    int mirror_uploads(int keep_days);
    // Writes <data_dir>/backups/photos.zip with every uploaded file (for an
    // off-server copy) and returns its path; "" if there are no uploads.
    std::string build_upload_archive();
    struct UploadStats { int files = 0; std::uintmax_t bytes = 0; };
    UploadStats upload_stats() const;

private:
    SqliteDatabase& db_;
    std::string     dir_;
    std::string     uploads_;
};
