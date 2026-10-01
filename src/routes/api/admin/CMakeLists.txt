# src/routes/api/admin: Settings, audit log, Discord role mappings and the match queue.
target_sources(lug_core PRIVATE
  AuditLogApiRoutes.cpp
  AuditLogApiRoutes.hpp
  PendingDiscordMatchesApiRoutes.cpp
  PendingDiscordMatchesApiRoutes.hpp
  RoleMappingsApiRoutes.cpp
  RoleMappingsApiRoutes.hpp
  SettingsApiRoutes.cpp
  SettingsApiRoutes.hpp
)
