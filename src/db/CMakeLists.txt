# src/db: SQLite connection, statements, transactions and migrations (sql/migrations).
target_sources(lug_core PRIVATE
  Migrations.cpp
  Migrations.hpp
  SqliteDatabase.cpp
  SqliteDatabase.hpp
)
