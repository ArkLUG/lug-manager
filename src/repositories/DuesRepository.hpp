#pragma once
#include "db/SqliteDatabase.hpp"
#include <string>
#include <vector>

struct DuesPayment {
    int64_t     id = 0;
    int64_t     member_id = 0;
    std::string paid_on;
    int64_t     amount_cents = 0;
    std::string method;
    std::string covers_until;
    std::string note;
    std::string recorded_by_name;
};

struct DuesStatusRow {
    int64_t     member_id = 0;
    std::string display_name;
    std::string discord_user_id;
    std::string paid_until;
};

// Dues ledger (migration 050) plus the expiry bookkeeping on members.
class DuesRepository {
public:
    explicit DuesRepository(SqliteDatabase& db);

    // Records a payment and extends the member's paid_until to at least
    // covers_until (never shortens it), marking them paid. Atomic.
    void record(int64_t member_id, const std::string& paid_on, int64_t amount_cents,
                const std::string& method, const std::string& covers_until,
                const std::string& note, int64_t recorded_by);
    std::vector<DuesPayment> history(int64_t member_id);
    bool remove(int64_t payment_id, int64_t member_id);

    // Members currently paid whose paid_until falls in [today, until].
    std::vector<DuesStatusRow> expiring_between(const std::string& today, const std::string& until);
    // Marks is_paid=0 for everyone whose paid_until is before today; returns them.
    std::vector<DuesStatusRow> expire_lapsed(const std::string& today);
    // Members due a reminder (expiring by `until`, not yet reminded for this paid_until).
    std::vector<DuesStatusRow> needing_reminder(const std::string& today, const std::string& until);
    void mark_reminded(int64_t member_id, const std::string& paid_until);
    SqliteDatabase& db() { return db_; }

private:
    SqliteDatabase& db_;
    std::vector<DuesStatusRow> rows(Statement& stmt);
};
