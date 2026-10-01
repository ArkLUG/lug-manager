# src/routes/attendance: Attendance pages, QR check-in, reports and CSV exports.
target_sources(lug_core PRIVATE
  AttendanceRoutes.cpp
  AttendanceRoutes.hpp
  CheckinRoutes.cpp
  CheckinRoutes.hpp
  ExportRoutes.cpp
  ExportRoutes.hpp
  ReportRoutes.cpp
  ReportRoutes.hpp
)
