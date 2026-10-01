#pragma once
#include "db/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <vector>

struct DisplayRequest {
    int64_t     id = 0;
    int64_t     event_id = 0;
    int64_t     member_id = 0;
    std::string member_display_name;
    std::string title;
    std::string description;
    int         width_in = 0;
    int         depth_in = 0;
    bool        needs_power = false;
    std::string notes;
    std::string status = "pending";      // pending | approved | declined
    std::string table_assignment;
    std::string created_at;
};

// Display/MOC space requests for events - see migration 048.
class DisplayRequestRepository {
public:
    explicit DisplayRequestRepository(SqliteDatabase& db);

    bool accepts_displays(int64_t event_id);
    void set_accepts_displays(int64_t event_id, bool on);

    DisplayRequest create(const DisplayRequest& r);
    std::optional<DisplayRequest> find(int64_t id);
    std::vector<DisplayRequest> list_for_event(int64_t event_id);
    std::vector<DisplayRequest> list_for_member(int64_t event_id, int64_t member_id);
    bool review(int64_t id, const std::string& status, const std::string& table_assignment);
    bool remove(int64_t id);

private:
    SqliteDatabase& db_;
    std::vector<DisplayRequest> query(const std::string& where, int64_t a, int64_t b = 0, bool two = false);
};
