#pragma once
#include "services/notifications/Notifier.hpp"
#include "services/Features.hpp"
#include <memory>
#include <string>

// Reminds borrowers once when a LUG item they hold reaches its due date
// (DM, or email for members without Discord; opt-out kind "loan_reminder").
class LoanReminders {
public:
    LoanReminders(SqliteDatabase& db, std::shared_ptr<Notifier> notifier) : db_(db), notifier_(std::move(notifier)) {}

    int run_once(const std::string& today) {
        if (!Features::on("inventory")) return 0;
        struct Due { int64_t id, member_id, qty; std::string item, due; };
        std::vector<Due> due;
        {
            auto st = db_.prepare(
                "SELECT l.id, l.member_id, l.quantity, i.name, l.due_on FROM inventory_loans l "
                "JOIN inventory_items i ON i.id = l.item_id "
                "WHERE l.returned_at IS NULL AND l.due_on <> '' AND l.due_on <= ? AND l.reminded_on = ''");
            st.bind(1, today);
            while (st.step()) due.push_back({st.col_int(0), st.col_int(1), st.col_int(2), st.col_text(3), st.col_text(4)});
        }
        int sent = 0;
        for (const auto& d : due) {
            // Mark first so a failed DM doesn't retry every 10 minutes.
            {
                auto up = db_.prepare("UPDATE inventory_loans SET reminded_on=? WHERE id=? AND reminded_on=''");
                up.bind(1, today); up.bind(2, d.id);
                up.step();
            }
            std::string what = (d.qty > 1 ? std::to_string(d.qty) + " x " : "") + d.item;
            std::string when = d.due == today ? "today" : "on " + d.due;
            if (notifier_->notify(d.member_id, "loan_reminder", "dm.loan_reminder", {{"item", what}, {"due", when}}))
                ++sent;
        }
        return sent;
    }

private:
    SqliteDatabase& db_;
    std::shared_ptr<Notifier> notifier_;
};
