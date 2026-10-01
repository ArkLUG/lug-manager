#pragma once
#include "utils/ImageUpload.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// Stores uploaded photos under <data_dir>/uploads with random names.
class PhotoStore {
public:
    static constexpr size_t kMaxBytes = 10 * 1024 * 1024;

    explicit PhotoStore(std::string data_dir) : dir_(std::move(data_dir) + "/uploads") {}

    // Validates, strips metadata and saves. Returns the stored name, or "" with `error` set.
    std::string save(const std::string& bytes, std::string& error) {
        if (bytes.empty()) { error = "Choose a photo."; return ""; }
        if (bytes.size() > kMaxBytes) { error = "Photo too large (max 10 MB)."; return ""; }
        PhotoInfo info = sniff_photo(bytes);
        if (!info.ok) { error = "That isn't a JPEG, PNG, GIF or WebP image."; return ""; }
        std::string clean = strip_photo_metadata(bytes, info);
        std::filesystem::create_directories(dir_);
        std::string name = new_upload_name(info.extension);
        std::ofstream out(dir_ + "/" + name, std::ios::binary);
        out.write(clean.data(), static_cast<std::streamsize>(clean.size()));
        if (!out) { error = "Couldn't save the photo."; return ""; }
        return name;
    }

    bool read(const std::string& name, std::string& bytes) const {
        if (!valid_upload_name(name)) return false;
        std::ifstream in(dir_ + "/" + name, std::ios::binary);
        if (!in) return false;
        std::ostringstream buf;
        buf << in.rdbuf();
        bytes = buf.str();
        return true;
    }

    void remove(const std::string& name) const {
        if (!valid_upload_name(name)) return;
        std::error_code ec;
        std::filesystem::remove(dir_ + "/" + name, ec);
    }

    static std::string content_type(const std::string& name) {
        auto ext = name.substr(name.rfind('.'));
        return ext == ".png" ? "image/png" : ext == ".gif" ? "image/gif" : ext == ".webp" ? "image/webp" : "image/jpeg";
    }

private:
    std::string dir_;
};
