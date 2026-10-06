#pragma once
#include "utils/web/ImageUpload.hpp"
#include <chrono>
#include <filesystem>
#include <set>
#include <fstream>
#include <sstream>
#include <string>

// Stores uploaded photos under <data_dir>/uploads with random names.
class PhotoStore {
public:
    static constexpr size_t kMaxBytes = 10 * 1024 * 1024;

    // subdir: "uploads" (gallery/challenges) or "uploads/receipts" (treasury).
    explicit PhotoStore(std::string data_dir, const std::string& subdir = "uploads")
        : dir_(std::move(data_dir) + "/" + subdir) {}

    // Receipts: a photo (as save()) or a PDF, stored as-is.
    std::string save_receipt(const std::string& bytes, std::string& error) {
        if (bytes.size() >= 5 && bytes.compare(0, 5, "%PDF-") == 0) {
            if (bytes.size() > kMaxBytes) { error = "File too large (max 10 MB)."; return ""; }
            std::filesystem::create_directories(dir_);
            std::string name = new_upload_name(".pdf");
            std::ofstream out(dir_ + "/" + name, std::ios::binary);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!out) { error = "Couldn't save the file."; return ""; }
            return name;
        }
        std::string name = save(bytes, error);
        if (name.empty() && !bytes.empty() && bytes.size() <= kMaxBytes) error = "A receipt must be a photo (JPEG, PNG, GIF, WebP) or a PDF.";
        return name;
    }

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

    // Deletes the files in this folder that nothing uses any more (`in_use`),
    // e.g. photos of a deleted event or member. Only files older than
    // `min_age`: a file saved a moment ago may not have its row yet.
    // Subfolders (receipts) are left alone. Returns how many went.
    int sweep(const std::set<std::string>& in_use, std::chrono::seconds min_age = std::chrono::hours(1)) const {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(dir_, ec)) return 0;
        const auto cutoff = fs::file_time_type::clock::now() - min_age;
        int n = 0;
        for (const auto& f : fs::directory_iterator(dir_, ec)) {
            if (!f.is_regular_file(ec)) continue;
            const std::string name = f.path().filename().string();
            if (in_use.count(name) || f.last_write_time(ec) > cutoff) continue;
            if (fs::remove(f.path(), ec)) ++n;
        }
        return n;
    }

    void remove(const std::string& name) const {
        if (!valid_upload_name(name)) return;
        std::error_code ec;
        std::filesystem::remove(dir_ + "/" + name, ec);
    }

    static std::string content_type(const std::string& name) {
        auto ext = name.substr(name.rfind('.'));
        if (ext == ".pdf") return "application/pdf";
        return ext == ".png" ? "image/png" : ext == ".gif" ? "image/gif" : ext == ".webp" ? "image/webp" : "image/jpeg";
    }

private:
    std::string dir_;
};
