# src/integrations/discord/sync: Member and role sync from the Discord server: role mappings, perk-tier roles, the rules for linking accounts.
target_sources(lug_core PRIVATE
  DiscordLinkPolicy.hpp
  MemberSyncService.cpp
  MemberSyncService.hpp
  PerkRoleSync.hpp
  RoleMappingRepository.cpp
  RoleMappingRepository.hpp
  RoleRoutes.cpp
  RoleRoutes.hpp
  RoleSync.hpp
)
