#pragma once
#include "models/LugEvent.hpp"
#include "models/Meeting.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/EventDayAttendanceRepository.hpp"
#include "services/EventService.hpp"
#include "services/MeetingService.hpp"

// Converts an event into a meeting: creates the meeting from the event's
// fields, copies every member who attended any event day as one meeting
// check-in (duplicates collapse via the attendance unique key), then cancels
// (deletes) the event. Shared by the browser route and the JSON API so the
// two can't drift apart. Throws on failure; the event is only cancelled
// after the meeting and its attendance exist.
inline Meeting convert_event_to_meeting(const LugEvent& ev,
                                        MeetingService& meetings,
                                        EventService& events,
                                        EventDayAttendanceRepository& event_day_attendance,
                                        AttendanceRepository& attendance) {
    Meeting m;
    m.title       = ev.title;
    m.description = ev.description;
    m.location    = ev.location;
    m.start_time  = ev.start_time;
    m.end_time    = ev.end_time;
    m.status      = "scheduled";
    m.scope       = ev.scope;
    m.chapter_id  = ev.chapter_id;
    Meeting created = meetings.create(m);

    for (const auto& a : event_day_attendance.find_by_event(ev.id)) {
        attendance.check_in(a.member_id, "meeting", created.id, a.notes, false);
    }

    events.cancel(ev.id);
    return created;
}
