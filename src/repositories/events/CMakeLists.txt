# src/repositories/events: Events, event days, meetings, attendance, RSVPs, shifts, display requests.
target_sources(lug_core PRIVATE
  AttendanceRepository.cpp
  AttendanceRepository.hpp
  DisplayRequestRepository.cpp
  DisplayRequestRepository.hpp
  EventDayAttendanceRepository.cpp
  EventDayAttendanceRepository.hpp
  EventBlocks.hpp
  EventDayRepository.cpp
  EventDayRepository.hpp
  EventRepository.cpp
  EventRepository.hpp
  MeetingRepository.cpp
  MeetingRepository.hpp
  RsvpRepository.cpp
  MeetingRsvps.hpp
  RsvpRepository.hpp
  ShiftRepository.cpp
  ShiftRepository.hpp
)
