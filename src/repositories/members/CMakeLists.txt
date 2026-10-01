# src/repositories/members: Members, dues, perk levels, chapters and chapter membership, notification preferences.
target_sources(lug_core PRIVATE
  ChapterMemberRepository.cpp
  ChapterMemberRepository.hpp
  ChapterRepository.cpp
  ChapterRepository.hpp
  DuesRepository.cpp
  DuesRepository.hpp
  MemberRepository.cpp
  MemberRepository.hpp
  NotificationPrefs.hpp
  PerkLevelRepository.cpp
  PerkLevelRepository.hpp
)
