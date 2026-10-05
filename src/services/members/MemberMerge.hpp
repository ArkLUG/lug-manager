#pragma once
#include "db/SqliteDatabase.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Merges a duplicate member record ("drop") into the one to keep: moves
// attendance, RSVPs, dues, displays, shifts, photos, challenge entries/votes,
// chapter roles and loans, fills the kept record's blank fields from the
// duplicate, then deletes the duplicate. Where both records have the same
// thing (e.g. both checked in to one meeting) the kept record's row wins.
class MemberMerge {
public:
    explicit MemberMerge(SqliteDatabase& db) : db_(db) {}

    // Rows the duplicate owns, per area (for the preview).
    std::vector<std::pair<std::string, int64_t>> counts(int64_t drop_id) {
        std::vector<std::pair<std::string, int64_t>> out;
        for (const auto& t : owned_tables()) {
            auto st = db_.prepare(std::string("SELECT COUNT(*) FROM ") + t.table + " WHERE member_id=?");
            st.bind(1, drop_id);
            int64_t n = st.step() ? st.col_int(0) : 0;
            if (n > 0) out.emplace_back(t.label, n);
        }
        return out;
    }

    // Throws std::runtime_error with a user-facing message if the merge isn't allowed.
    void merge(int64_t keep_id, int64_t drop_id) {
        if (keep_id == drop_id) throw std::runtime_error("Pick two different members.");
        Transaction tx(db_);
        std::string keep_discord, drop_discord, keep_role, drop_role;
        if (!load(keep_id, keep_discord, keep_role) || !load(drop_id, drop_discord, drop_role))
            throw std::runtime_error("One of those members no longer exists.");
        if (!keep_discord.empty() && !drop_discord.empty() && keep_discord != drop_discord)
            throw std::runtime_error("Both records are linked to different Discord accounts, so they're different people "
                                     "(or one Discord account should be unlinked first).");

        // Chapter roles: keep the higher one where both are in a chapter.
        exec("UPDATE chapter_members SET chapter_role = (SELECT d.chapter_role FROM chapter_members d "
             "  WHERE d.member_id=?2 AND d.chapter_id=chapter_members.chapter_id) "
             "WHERE member_id=?1 AND " + rank("chapter_role") + " < (SELECT " + rank("d.chapter_role") +
             "  FROM chapter_members d WHERE d.member_id=?2 AND d.chapter_id=chapter_members.chapter_id)",
             keep_id, drop_id);

        for (const auto& t : owned_tables()) {
            exec(std::string("UPDATE OR IGNORE ") + t.table + " SET member_id=?1 WHERE member_id=?2", keep_id, drop_id);
            exec(std::string("DELETE FROM ") + t.table + " WHERE member_id=?2", keep_id, drop_id);
        }
        static const char* refs[][2] = {
            {"api_keys", "created_by"}, {"challenges", "created_by"}, {"chapter_members", "granted_by"},
            {"chapters", "created_by"}, {"dues_payments", "recorded_by"}, {"inventory_loans", "checked_out_by"},
            {"lug_events", "event_lead_id"}, {"meeting_series", "created_by"},
            {"pending_discord_matches", "suggested_member_id"}, {"pending_discord_matches", "resolved_member_id"},
            {"treasury_entries", "recorded_by"}, {"storage_locations", "keeper_id"},
            {"storage_locations", "owner_member_id"}, {"inventory_items", "owner_member_id"},
            {"community_ambassador_terms", "member_id"}, {"fan_colab_task_done", "done_by"},
        };
        for (const auto& r : refs)
            exec(std::string("UPDATE ") + r[0] + " SET " + r[1] + "=?1 WHERE " + r[1] + "=?2", keep_id, drop_id);

        // The Community Ambassador setting follows the person.
        exec("UPDATE lug_settings SET value=CAST(?1 AS TEXT) WHERE key='community_ambassador_id' AND value=CAST(?2 AS TEXT)",
             keep_id, drop_id);
        // Fill blanks on the kept record from the duplicate.
        static const char* text_cols[] = {"discord_username", "email", "phone", "address_line1", "address_line2", "city",
                                          "state", "zip", "birthday", "guardian_name", "guardian_phone", "guardian_email",
                                          "consent_date", "first_name", "last_name"};
        for (const char* c : text_cols)
            exec(std::string("UPDATE members SET ") + c + " = COALESCE(NULLIF(" + c + ",''), (SELECT " + c +
                 " FROM members WHERE id=?2)) WHERE id=?1", keep_id, drop_id);
        exec("UPDATE members SET "
             "is_paid = MAX(is_paid, (SELECT is_paid FROM members WHERE id=?2)), "
             "paid_until = CASE WHEN COALESCE((SELECT paid_until FROM members WHERE id=?2),'') > COALESCE(paid_until,'') "
             "  THEN (SELECT paid_until FROM members WHERE id=?2) ELSE paid_until END, "
             "consent_on_file = MAX(consent_on_file, (SELECT consent_on_file FROM members WHERE id=?2)), "
             "photo_release = MAX(photo_release, (SELECT photo_release FROM members WHERE id=?2)), "
             "is_treasurer = MAX(is_treasurer, (SELECT is_treasurer FROM members WHERE id=?2)) "
             "WHERE id=?1", keep_id, drop_id);
        if (role_rank(drop_role) > role_rank(keep_role))
            exec("UPDATE members SET role=(SELECT role FROM members WHERE id=?2), "
                 "role_source=(SELECT role_source FROM members WHERE id=?2) WHERE id=?1", keep_id, drop_id);
        if (keep_discord.empty() && !drop_discord.empty()) {
            exec("UPDATE members SET discord_user_id=NULL WHERE id=?2", keep_id, drop_id);
            auto st = db_.prepare("UPDATE members SET discord_user_id=? WHERE id=?");
            st.bind(1, drop_discord); st.bind(2, keep_id);
            st.step();
        }
        exec("DELETE FROM members WHERE id=?2", keep_id, drop_id);
        tx.commit();
    }

    static int role_rank(const std::string& r) {
        if (r == "admin") return 4;
        if (r == "moderator" || r == "chapter_lead") return 3;
        if (r == "member") return 2;
        return 1;
    }

private:
    struct Owned { const char* table; const char* label; };
    static const std::vector<Owned>& owned_tables() {
        static const std::vector<Owned> t = {
            {"attendance", "Meeting/event check-ins"}, {"event_day_attendance", "Event day check-ins"},
            {"event_rsvps", "RSVPs"}, {"event_display_requests", "Display requests"},
            {"dues_payments", "Dues payments"}, {"event_shift_signups", "Volunteer shifts"},
            {"event_photos", "Event photos"}, {"challenge_entries", "Challenge entries"},
            {"challenge_votes", "Challenge votes"}, {"chapter_members", "Chapter memberships"},
            {"inventory_loans", "Inventory loans"}, {"notification_optouts", "Notification choices"},
        };
        return t;
    }
    static std::string rank(const std::string& col) {
        return "(CASE " + col + " WHEN 'lead' THEN 2 WHEN 'event_manager' THEN 1 ELSE 0 END)";
    }
    bool load(int64_t id, std::string& discord, std::string& role) {
        auto st = db_.prepare("SELECT COALESCE(discord_user_id,''), role FROM members WHERE id=?");
        st.bind(1, id);
        if (!st.step()) return false;
        discord = st.col_text(0); role = st.col_text(1);
        return true;
    }
    void exec(const std::string& sql, int64_t keep_id, int64_t drop_id) {
        auto st = db_.prepare(sql);
        st.bind(1, keep_id); st.bind(2, drop_id);
        st.step();
    }

    SqliteDatabase& db_;
};
