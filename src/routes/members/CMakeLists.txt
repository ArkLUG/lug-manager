# src/routes/members: Members (incl. bulk edits and merging), dues, perk levels, chapters.
target_sources(lug_core PRIVATE
  ChapterRoutes.cpp
  ChapterRoutes.hpp
  DuesRoutes.cpp
  DuesRoutes.hpp
  MemberBulkRoutes.cpp
  MemberBulkRoutes.hpp
  MemberMergeRoutes.cpp
  MemberMergeRoutes.hpp
  MemberRoutes.cpp
  MemberRoutes.hpp
  PerkRoutes.cpp
  PerkRoutes.hpp
)
