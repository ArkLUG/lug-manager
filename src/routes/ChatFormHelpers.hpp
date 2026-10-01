#pragma once
// The "Discord: leave out parts" options on the meeting/event forms
// (stored per item by chat::ChatHub::set_skipped).
#include "chat/ChatHub.hpp"
#include <crow/mustache.h>
#include <functional>
#include <string>

// "\x01" (leave as is) unless the form had the options block.
inline std::string chat_skip_from_form(const std::function<std::string(const char*)>& get, const std::string& entity_type) {
    if (get("chat_opts") != "1") return "\x01";
    static const char* event_parts[] = {"announce", "chapter_announce", "thread", "scheduled", "update_note"};
    static const char* meeting_parts[] = {"announce", "scheduled"};
    std::string out;
    auto add = [&](const char* part) {
        std::string v = get((std::string("skip_") + part).c_str());
        if (v == "1" || v == "on") out += (out.empty() ? "" : ",") + std::string(part);
    };
    if (entity_type == "event") for (const char* p : event_parts) add(p);
    else for (const char* p : meeting_parts) add(p);
    return out;
}

inline void add_chat_skip_ctx(crow::mustache::context& ctx, chat::ChatHub* hub, const std::string& entity_type, int64_t id) {
    if (!hub || id <= 0) return;
    auto skip = hub->skipped(entity_type, id);
    for (const auto& s : skip) ctx["skip_" + s] = true;
    ctx["has_chat_skip"] = !skip.empty();
}
