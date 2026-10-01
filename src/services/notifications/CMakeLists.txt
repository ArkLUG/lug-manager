# src/services/notifications: Reminders, the weekly digest, loan reminders, and the Notifier (chat DM or email).
target_sources(lug_core PRIVATE
  DigestService.hpp
  LoanReminders.hpp
  Notifier.hpp
  ReminderService.cpp
  ReminderService.hpp
)
