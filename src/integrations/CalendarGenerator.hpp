#pragma once
#include "config/Config.hpp"
#include "repositories/MeetingRepository.hpp"
#include "repositories/EventRepository.hpp"
#include <string>
#include <mutex>
#include <chrono>

class CalendarGenerator {
public:
    CalendarGenerator(MeetingRepository& meetings, EventRepository& events, const Config& config);

    // Returns ICS string (cached for up to 5 minutes)
    std::string get_ics();

    // Invalidate cache - call after any meeting/event create/update/cancel
    void invalidate();

    // Update timezone at runtime (called when lug_timezone setting changes)
    void set_timezone(const std::string& tz);

private:
    MeetingRepository& meetings_;
    EventRepository&   events_;
    const Config&      config_;
    std::string        timezone_;
    std::string        calendar_name_;

    std::string                           cached_ics_;
    std::chrono::steady_clock::time_point cache_time_;
    bool                                  cache_valid_ = false;
    mutable std::mutex                    mutex_;

    std::string generate_ics() const;
    static std::string escape_ical(const std::string& s);
    static std::string iso_to_ical_dt(const std::string& iso);
    static std::string fold_line(const std::string& prop, const std::string& val);
    static std::string make_vevent(const std::string& uid,
                                   const std::string& summary,
                                   const std::string& description,
                                   const std::string& location,
                                   const std::string& start,
                                   const std::string& end,
                                   const std::string& status,
                                   const std::string& last_modified,
                                   const std::string& timezone);
};
