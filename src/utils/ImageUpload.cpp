#include "utils/ImageUpload.hpp"
#include "auth/SessionStore.hpp"
#include <regex>

PhotoInfo sniff_photo(const std::string& b) {
    auto u = [&](size_t i) { return static_cast<unsigned char>(b[i]); };
    if (b.size() >= 8 && u(0) == 0x89 && b.compare(1, 3, "PNG") == 0) return {true, ".png", "image/png"};
    if (b.size() >= 3 && u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF) return {true, ".jpg", "image/jpeg"};
    if (b.size() >= 6 && (b.compare(0, 6, "GIF87a") == 0 || b.compare(0, 6, "GIF89a") == 0)) return {true, ".gif", "image/gif"};
    if (b.size() >= 12 && b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0) return {true, ".webp", "image/webp"};
    return {};
}

namespace {

// JPEG: copy segments, dropping APP1 (EXIF/XMP), APP13 (IPTC) and COM.
std::string strip_jpeg(const std::string& b) {
    std::string out = b.substr(0, 2); // SOI
    size_t i = 2;
    while (i + 4 <= b.size()) {
        if (static_cast<unsigned char>(b[i]) != 0xFF) return b; // not a marker - bail out unchanged
        unsigned char marker = static_cast<unsigned char>(b[i + 1]);
        if (marker == 0xDA) { out.append(b, i, std::string::npos); return out; } // SOS: rest is image data
        size_t len = (static_cast<unsigned char>(b[i + 2]) << 8) | static_cast<unsigned char>(b[i + 3]);
        if (len < 2 || i + 2 + len > b.size()) return b;
        bool drop = marker == 0xE1 || marker == 0xED || marker == 0xFE;
        if (!drop) out.append(b, i, 2 + len);
        i += 2 + len;
    }
    return b;
}

// PNG: drop eXIf / tEXt / iTXt / zTXt chunks.
std::string strip_png(const std::string& b) {
    std::string out = b.substr(0, 8);
    size_t i = 8;
    while (i + 12 <= b.size()) {
        uint32_t len = (static_cast<unsigned char>(b[i]) << 24) | (static_cast<unsigned char>(b[i + 1]) << 16) |
                       (static_cast<unsigned char>(b[i + 2]) << 8) | static_cast<unsigned char>(b[i + 3]);
        if (i + 12 + static_cast<size_t>(len) > b.size()) return b;
        std::string type = b.substr(i + 4, 4);
        if (type != "eXIf" && type != "tEXt" && type != "iTXt" && type != "zTXt") out.append(b, i, 12 + len);
        i += 12 + len;
        if (type == "IEND") break;
    }
    return out;
}

// WebP: drop EXIF / XMP chunks and fix the RIFF size (VP8X flags keep
// claiming metadata, which decoders tolerate).
std::string strip_webp(const std::string& b) {
    std::string out = b.substr(0, 12);
    size_t i = 12;
    while (i + 8 <= b.size()) {
        uint32_t len = static_cast<unsigned char>(b[i + 4]) | (static_cast<unsigned char>(b[i + 5]) << 8) |
                       (static_cast<unsigned char>(b[i + 6]) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(b[i + 7])) << 24);
        size_t padded = len + (len & 1);
        if (i + 8 + padded > b.size()) return b;
        std::string type = b.substr(i, 4);
        if (type != "EXIF" && type != "XMP ") out.append(b, i, 8 + padded);
        i += 8 + padded;
    }
    uint32_t riff = static_cast<uint32_t>(out.size() - 8);
    out[4] = static_cast<char>(riff & 0xFF); out[5] = static_cast<char>((riff >> 8) & 0xFF);
    out[6] = static_cast<char>((riff >> 16) & 0xFF); out[7] = static_cast<char>((riff >> 24) & 0xFF);
    return out;
}

} // namespace

std::string strip_photo_metadata(const std::string& bytes, const PhotoInfo& info) {
    if (info.extension == ".jpg")  return strip_jpeg(bytes);
    if (info.extension == ".png")  return strip_png(bytes);
    if (info.extension == ".webp") return strip_webp(bytes);
    return bytes;
}

std::string new_upload_name(const std::string& extension) {
    return SessionStore::generate_token().substr(0, 32) + extension;
}

bool valid_upload_name(const std::string& name) {
    static const std::regex re(R"([0-9a-f]{32}\.(jpg|png|gif|webp))");
    return std::regex_match(name, re);
}
