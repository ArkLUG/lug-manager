# src/routes/settings: Settings pages: Discord/chat, message wording, features, branding, backups, setup, audit log.
target_sources(lug_core PRIVATE
  AuditRoutes.cpp
  AuditRoutes.hpp
  BackupRoutes.cpp
  BackupRoutes.hpp
  BrandingRoutes.cpp
  BrandingRoutes.hpp
  ChatSettingsRoutes.cpp
  ChatSettingsRoutes.hpp
  FeatureRoutes.cpp
  FeatureRoutes.hpp
  ReminderSettingsRoutes.cpp
  ReminderSettingsRoutes.hpp
  SettingsRoutes.cpp
  SettingsRoutes.hpp
  SetupRoutes.cpp
  SetupRoutes.hpp
)
