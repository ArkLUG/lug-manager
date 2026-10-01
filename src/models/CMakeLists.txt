# src/models: Plain structs for database rows.
target_sources(lug_core PRIVATE
  ApiKey.hpp
  Attendance.hpp
  AuditLog.hpp
  Chapter.hpp
  ChapterMember.hpp
  EventDay.hpp
  LugEvent.hpp
  Meeting.hpp
  Member.hpp
  PendingDiscordMatch.hpp
  PerkLevel.hpp
  RoleMapping.hpp
  Session.hpp
)
