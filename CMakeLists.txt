cmake_minimum_required(VERSION 3.20)
project(lug_manager CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Standalone Asio (required by Crow, before Crow is configured)
FetchContent_Declare(asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-32-0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asio)
set(ASIO_INCLUDE_DIR ${asio_SOURCE_DIR}/asio/include CACHE PATH "asio include dir" FORCE)

# Crow (header-only web framework)
FetchContent_Declare(Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG        v1.2.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Crow)

# nlohmann/json
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# System libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL    REQUIRED libcurl)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
find_package(OpenSSL REQUIRED)

# Collect all source files
file(GLOB_RECURSE SOURCES
  src/config/*.cpp
  src/db/*.cpp
  src/models/*.cpp
  src/repositories/*.cpp
  src/services/*.cpp
  src/integrations/*.cpp
  src/async/*.cpp
  src/auth/*.cpp
  src/routes/*.cpp
  src/main.cpp
)

add_executable(lug_manager ${SOURCES})

target_include_directories(lug_manager PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CURL_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
  ${ASIO_INCLUDE_DIR}
)

target_link_libraries(lug_manager PRIVATE
  Crow::Crow
  nlohmann_json::nlohmann_json
  ${CURL_LIBRARIES}
  ${SQLITE3_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread
)

target_compile_options(lug_manager PRIVATE -Wall -Wextra -O2)

# Tell Crow where to find static files (served at /static/<path>)
target_compile_definitions(lug_manager PRIVATE CROW_STATIC_DIRECTORY="src/static/")
