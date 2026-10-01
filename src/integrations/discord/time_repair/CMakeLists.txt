# src/integrations/discord/time_repair: Silently corrects event/meeting times that older versions sent to Discord as UTC.
target_sources(lug_core PRIVATE
  DiscordTimeRepair.hpp
  DiscordTimeRepairRoutes.cpp
  DiscordTimeRepairRoutes.hpp
)
