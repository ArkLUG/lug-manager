# src/routes/events: Events and what hangs off them: RSVPs, volunteer shifts, display requests, the public shows page.
target_sources(lug_core PRIVATE
  DisplayRoutes.cpp
  DisplayRoutes.hpp
  EventAccess.hpp
  EventRoutes.cpp
  EventRoutes.hpp
  RsvpRoutes.cpp
  RsvpRoutes.hpp
  ShiftRoutes.cpp
  ShiftRoutes.hpp
  ShowsRoutes.cpp
  ShowsRoutes.hpp
)
