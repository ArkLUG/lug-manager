# tests - unit/ (no server), integration/<area>/ (the full app on a local
# port, with fake Discord/Google servers), support/ (fixtures and fakes).
# e2e/ holds browser checks (Python + Selenium), run by hand.
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
  GIT_SHALLOW    TRUE
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# One test executable per file, named after it.
function(lug_add_tests)
  foreach(test_file ${ARGN})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} PRIVATE lug_core gtest gtest_main)
    target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
    set_target_properties(${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endforeach()
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
