# src/routes/accounts: Sign-in (Discord, password, email links, two-factor), My Account, sessions, API keys.
target_sources(lug_core PRIVATE
  AccountRoutes.cpp
  AccountRoutes.hpp
  AccountSecurityRoutes.cpp
  AccountSecurityRoutes.hpp
  ApiKeyRoutes.cpp
  ApiKeyRoutes.hpp
  AuthRoutes.cpp
  AuthRoutes.hpp
  SessionRoutes.cpp
  SessionRoutes.hpp
)
