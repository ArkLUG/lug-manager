#pragma once
#include <crow/mustache.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

// Replaces Crow's mustache loader (which re-reads the template file on every
// crow::mustache::load - twice per full page) with an in-memory cache keyed
// by path. The file's mtime is checked on each use, so edited templates
// still show up without a restart in development.
inline void install_template_cache() {
    struct Entry { std::filesystem::file_time_type mtime; std::string text; };
    static std::mutex mtx;
    static std::unordered_map<std::string, Entry> cache;
    crow::mustache::set_loader([](std::string filename) -> std::string {
        std::string path = crow::utility::join_path(
            crow::mustache::detail::get_template_base_directory_ref(), filename);
        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(path, ec);
        if (ec) return {};
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = cache.find(path);
            if (it != cache.end() && it->second.mtime == mtime) return it->second.text;
        }
        std::ifstream in(path);
        std::ostringstream buf;
        buf << in.rdbuf();
        std::lock_guard<std::mutex> lock(mtx);
        cache[path] = Entry{mtime, buf.str()};
        return cache[path].text;
    });
}
