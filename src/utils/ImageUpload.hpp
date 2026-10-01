#pragma once
#include <cstdint>
#include <string>

// Raster photo uploads (gallery, build challenges): JPEG, PNG, GIF, WebP only
// (never SVG - it can carry script). Identified by magic bytes, and stripped
// of EXIF/XMP metadata, which on phone photos includes GPS coordinates.
struct PhotoInfo {
    bool        ok = false;
    std::string extension;     // ".jpg" ...
    std::string content_type;  // "image/jpeg" ...
};

PhotoInfo sniff_photo(const std::string& bytes);
// Returns the image without metadata segments/chunks (or unchanged if the
// format has none we know how to remove).
std::string strip_photo_metadata(const std::string& bytes, const PhotoInfo& info);
// Random file name "<32 hex><ext>" for storing an upload.
std::string new_upload_name(const std::string& extension);
// True for names produced by new_upload_name (safe to join to a directory).
// ".pdf" is only ever produced for treasury receipts.
bool valid_upload_name(const std::string& name);
