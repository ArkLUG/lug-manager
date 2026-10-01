# src/integrations/discord/matches: New Discord members waiting to be matched to member records, and the button interactions that resolve them.
target_sources(lug_core PRIVATE
  DiscordInteractionsRoutes.cpp
  DiscordInteractionsRoutes.hpp
  DiscordMatchRoutes.cpp
  DiscordMatchRoutes.hpp
  PendingDiscordMatchRepository.cpp
  PendingDiscordMatchRepository.hpp
)
