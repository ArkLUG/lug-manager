# src/integrations/discord: Discord: the REST client, OAuth sign-in, interaction signatures, the chat Provider and id checks. Subfolders: sync/, matches/, time_repair/.
target_sources(lug_core PRIVATE
  DiscordClient.cpp
  DiscordClient.hpp
  DiscordOAuth.cpp
  DiscordOAuth.hpp
  DiscordProvider.hpp
  DiscordSignatureVerifier.cpp
  DiscordSignatureVerifier.hpp
  Snowflake.hpp
)
add_subdirectory(matches)
add_subdirectory(sync)
add_subdirectory(time_repair)
