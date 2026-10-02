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
  PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_crow_mustache.cmake
                 COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_crow_body_limit.cmake
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

# md4c (C Markdown parser)
FetchContent_Declare(md4c
  GIT_REPOSITORY https://github.com/mity/md4c.git
  GIT_TAG        release-0.5.2
  GIT_SHALLOW    TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(md4c)

# System libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL    REQUIRED libcurl)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
find_package(OpenSSL REQUIRED)

# ─── The app ────────────────────────────────────────────────────────────────
# Everything except main() is the lug_core library, which the server and the
# tests both link. Each folder under src/ has its own CMakeLists.txt that adds
# its files to it (listed by name - add new files there).
find_package(ZLIB REQUIRED)
add_library(lug_core STATIC)
target_include_directories(lug_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CURL_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
  ${ASIO_INCLUDE_DIR}
  ${md4c_SOURCE_DIR}/src
)
# /static is served by StaticRoutes (caching + ETags); Crow's built-in static
# handler is disabled. Compression gzips dynamic responses and text assets.
target_compile_definitions(lug_core PUBLIC CROW_DISABLE_STATIC_DIR CROW_ENABLE_COMPRESSION)
target_compile_options(lug_core PRIVATE -Wall -Wextra -O2)
# Version for the About page: -DLUG_VERSION=v2.0.2 (CI/Docker), else git describe, else "dev".
if(NOT LUG_VERSION)
  execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE LUG_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT LUG_VERSION)
    set(LUG_VERSION "dev")
  endif()
endif()
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/routes/pages/HelpRoutes.cpp PROPERTIES
                            COMPILE_DEFINITIONS "LUG_VERSION=\"${LUG_VERSION}\"")
target_link_libraries(lug_core PUBLIC
  Crow::Crow
  nlohmann_json::nlohmann_json
  md4c-html
  ZLIB::ZLIB
  ${CURL_LIBRARIES}
  ${SQLITE3_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  pthread
)

add_subdirectory(src)

# ─── Tests ──────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit and integration tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
