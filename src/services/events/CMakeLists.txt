# src/services/events: Events, meetings, series, attendance; event-to-meeting conversion.
target_sources(lug_core PRIVATE
  AttendanceService.cpp
  AttendanceService.hpp
  EventConversion.hpp
  EventService.cpp
  EventService.hpp
  MeetingService.cpp
  MeetingService.hpp
  SeriesService.cpp
  SeriesService.hpp
  TimeCheck.hpp
)
