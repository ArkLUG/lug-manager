# src/repositories/admin: Settings, audit log and API keys.
target_sources(lug_core PRIVATE
  ApiKeyRepository.cpp
  ApiKeyRepository.hpp
  AuditLogRepository.cpp
  AuditLogRepository.hpp
  SettingsRepository.cpp
  SettingsRepository.hpp
)
