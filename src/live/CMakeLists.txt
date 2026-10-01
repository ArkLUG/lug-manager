# src/live: Live updates - a websocket that tells open pages what changed (static/live.js reloads those parts).
target_sources(lug_core PRIVATE
  LiveHub.cpp
  LiveHub.hpp
  LiveRoutes.cpp
  LiveRoutes.hpp
)
