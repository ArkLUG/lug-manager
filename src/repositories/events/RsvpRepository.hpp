#pragma once
#include "db/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <vector>

struct EventRsvp {
    int64_t     id = 0;
    int64_t     event_id = 0;
    int64_t     member_id = 0;
    std::string status;       // "going" | "waitlist"
    std::string created_at;
    std::string member_display_name;
};

// Event RSVPs with a capacity limit and a first-come waitlist - see
// migration 047. Capacity is the event's max_attendees (0 = unlimited).
class RsvpRepository {
public:
    explicit RsvpRepository(SqliteDatabase& db);

    // Adds the member's RSVP if they don't have one: "going" while there is
    // room, else "waitlist". Returns the member's resulting status.
    std::string rsvp(int64_t event_id, int64_t member_id, int max_attendees);

    // Removes the member's RSVP. If it freed a "going" spot, promotes the
    // oldest waitlisted member and returns their member id.
    std::optional<int64_t> cancel(int64_t event_id, int64_t member_id, int max_attendees);

    std::optional<std::string> status_of(int64_t event_id, int64_t member_id);
    // 1-based position on the waitlist, 0 if not waitlisted.
    int  waitlist_position(int64_t event_id, int64_t member_id);
    int  count(int64_t event_id, const std::string& status);
    std::vector<EventRsvp> list(int64_t event_id); // going first, then waitlist, oldest first

private:
    SqliteDatabase& db_;
};
