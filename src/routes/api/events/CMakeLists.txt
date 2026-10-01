# src/routes/api/events: Events, meetings, attendance.
target_sources(lug_core PRIVATE
  AttendanceApiRoutes.cpp
  AttendanceApiRoutes.hpp
  EventsApiRoutes.cpp
  EventsApiRoutes.hpp
  MeetingsApiRoutes.cpp
  MeetingsApiRoutes.hpp
)
