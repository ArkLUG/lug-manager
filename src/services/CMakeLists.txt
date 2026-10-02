# src/services: Business logic between routes and repositories.
target_sources(lug_core PRIVATE
  AuditService.hpp
  BackupService.cpp
  BackupService.hpp
  FanCoLab.hpp
  Features.hpp
  Palettes.hpp
  PhotoStore.hpp
  SiteSettings.hpp
)
add_subdirectory(events)
add_subdirectory(members)
add_subdirectory(notifications)
