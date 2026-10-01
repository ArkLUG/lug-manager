# src/utils: Small helpers with no app state.
target_sources(lug_core PRIVATE
  AuditDiff.hpp
  Crypto.cpp
  Crypto.hpp
  LocalTime.hpp
  Offline.hpp
)
add_subdirectory(text)
add_subdirectory(web)
