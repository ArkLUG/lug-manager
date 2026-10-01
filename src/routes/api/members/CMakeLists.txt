# src/routes/api/members: Members, chapters, chapter membership, perk levels.
target_sources(lug_core PRIVATE
  ChapterMembersApiRoutes.cpp
  ChapterMembersApiRoutes.hpp
  ChaptersApiRoutes.cpp
  ChaptersApiRoutes.hpp
  MembersApiRoutes.cpp
  MembersApiRoutes.hpp
  PerkLevelsApiRoutes.cpp
  PerkLevelsApiRoutes.hpp
)
