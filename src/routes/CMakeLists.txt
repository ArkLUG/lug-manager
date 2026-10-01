# src/routes: HTTP routes by area. Router.cpp registers them all.
target_sources(lug_core PRIVATE
  ChatFormHelpers.hpp
  Router.cpp
  Router.hpp
)
add_subdirectory(accounts)
add_subdirectory(api)
add_subdirectory(attendance)
add_subdirectory(community)
add_subdirectory(events)
add_subdirectory(meetings)
add_subdirectory(members)
add_subdirectory(pages)
add_subdirectory(settings)
