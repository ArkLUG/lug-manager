# src/utils/web: Request/response helpers: form bodies, client IP, ids, asset versions, template cache, uploads, zip.
target_sources(lug_core PRIVATE
  AssetVersion.hpp
  CalendarLinks.hpp
  ClientIp.hpp
  FormBody.hpp
  ImageUpload.cpp
  ImageUpload.hpp
  ParseId.hpp
  TemplateCache.hpp
  ZipWriter.hpp
)
