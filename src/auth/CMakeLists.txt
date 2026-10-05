# src/auth: Sign-in building blocks: sessions, passwords (scrypt), TOTP two-factor, recovery codes, reset tokens.
target_sources(lug_core PRIVATE
  AccountSecurity.hpp
  AuthService.cpp
  AuthService.hpp
  Permissions.hpp
  Password.hpp
  SessionStore.cpp
  SessionStore.hpp
  Totp.hpp
)
