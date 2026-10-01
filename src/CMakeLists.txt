# src - the app: every folder adds its files to the lug_core library; main.cpp is the server.
add_subdirectory(async)
add_subdirectory(auth)
add_subdirectory(chat)
add_subdirectory(config)
add_subdirectory(db)
add_subdirectory(integrations)
add_subdirectory(middleware)
add_subdirectory(models)
add_subdirectory(repositories)
add_subdirectory(routes)
add_subdirectory(services)
add_subdirectory(utils)

# The server
add_executable(lug_manager main.cpp)
target_link_libraries(lug_manager PRIVATE lug_core)
target_compile_options(lug_manager PRIVATE -Wall -Wextra -O2)
set_target_properties(lug_manager PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})   # build/lug_manager
