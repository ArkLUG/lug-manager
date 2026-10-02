# src/services/members: Members, merging, dues, perk progress, chapters.
target_sources(lug_core PRIVATE
  ChapterService.cpp
  ChapterService.hpp
  DuesProration.hpp
  DuesService.hpp
  MemberMerge.hpp
  MemberService.cpp
  MemberService.hpp
  PerkProgress.hpp
)
