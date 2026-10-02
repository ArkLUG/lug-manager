# src/routes/pages: Dashboard and calendar feeds, help, PWA files, /static.
target_sources(lug_core PRIVATE
  CalendarRoutes.cpp
  CalendarRoutes.hpp
  DashboardWidgets.hpp
  HealthRoutes.cpp
  HealthRoutes.hpp
  HelpRoutes.cpp
  HelpRoutes.hpp
  PwaRoutes.cpp
  PwaRoutes.hpp
  StaticRoutes.cpp
  StaticRoutes.hpp
)
