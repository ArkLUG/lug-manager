#pragma once
#include <crow.h>
#include <cstdint>
#include <string>

// Fields of a urlencoded form POST, trimmed to a maximum length.
struct FormBody {
    crow::query_string q;
    size_t default_max;
    explicit FormBody(const crow::request& req, size_t default_max = 200)
        : q("?" + req.body), default_max(default_max) {}
    std::string get(const char* k) const { return get(k, default_max); }
    std::string get(const char* k, size_t max) const {
        const char* v = q.get(k);
        return v ? std::string(v).substr(0, max) : "";
    }
    int64_t num(const char* k, int64_t def = 0) const {
        try { return std::stoll(get(k, 20)); } catch (...) { return def; }
    }
};
