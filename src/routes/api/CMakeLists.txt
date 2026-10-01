# src/routes/api: The JSON API (/api/v1, API-key auth), by area. Spec: src/static/openapi.yaml.
target_sources(lug_core PRIVATE
  ApiCommon.hpp
  Serialize.hpp
)
add_subdirectory(admin)
add_subdirectory(events)
add_subdirectory(members)
