#pragma once
#include "db/SqliteDatabase.hpp"
#include <set>
#include <string>

// Every uploaded file something still refers to: event photos, challenge
// entries, inventory item photos, the public About page, and (though they
// live in uploads/receipts/, which the sweep never enters) treasury receipts.
// Anything else in uploads/ is left over from something deleted
// (PhotoStore::sweep). The logo, secret.key, the database and backups live in
// the data folder itself, which the sweep never looks at.
inline std::set<std::string> uploads_in_use(SqliteDatabase& db) {
    std::set<std::string> in_use;
    for (const char* sql : {"SELECT file FROM event_photos", "SELECT file FROM challenge_entries",
                            "SELECT photo_file FROM inventory_items WHERE photo_file<>''", "SELECT file FROM about_photos",
                            "SELECT receipt_file FROM treasury_entries WHERE receipt_file<>''"}) {
        auto st = db.prepare(sql);
        while (st.step()) in_use.insert(st.col_text(0));
    }
    return in_use;
}
