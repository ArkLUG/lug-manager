#pragma once
#include "db/SqliteDatabase.hpp"
#include <set>
#include <string>

// Every file in <data_dir>/uploads that something still refers to: event
// photos, challenge entries, inventory item photos and the public About page.
// Anything else there is left over from something deleted (PhotoStore::sweep).
inline std::set<std::string> uploads_in_use(SqliteDatabase& db) {
    std::set<std::string> in_use;
    for (const char* sql : {"SELECT file FROM event_photos", "SELECT file FROM challenge_entries",
                            "SELECT photo_file FROM inventory_items WHERE photo_file<>''", "SELECT file FROM about_photos"}) {
        auto st = db.prepare(sql);
        while (st.step()) in_use.insert(st.col_text(0));
    }
    return in_use;
}
