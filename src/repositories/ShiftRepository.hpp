#pragma once
#include "db/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <vector>

struct EventShift {
    int64_t     id = 0, event_id = 0;
    std::string title, starts_at, ends_at, notes;
    int         slots = 1;
    int         taken = 0;
    std::vector<std::pair<int64_t, std::string>> volunteers; // member id, display name
};

// Volunteer shifts (migration 055).
class ShiftRepository {
public:
    explicit ShiftRepository(SqliteDatabase& db) : db_(db) {}

    int64_t create(const EventShift& s);
    bool    remove(int64_t shift_id, int64_t event_id);
    std::optional<EventShift> find(int64_t shift_id);
    std::vector<EventShift> for_event(int64_t event_id);
    // Signs up if there's a free slot. Returns false if full or already signed up.
    bool sign_up(int64_t shift_id, int64_t member_id);
    bool withdraw(int64_t shift_id, int64_t member_id);
    bool is_signed_up(int64_t shift_id, int64_t member_id);

    struct Due { int64_t signup_id; int64_t member_id; std::string discord_user_id, display_name, shift_title, event_title, starts_at; };
    // Signups whose shift starts in [from, to] (LUG-local ISO) and haven't been reminded.
    std::vector<Due> due_reminders(const std::string& from, const std::string& to);
    bool claim_reminder(int64_t signup_id);

private:
    SqliteDatabase& db_;
};
