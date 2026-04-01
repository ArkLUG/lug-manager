#include "services/ChapterService.hpp"

ChapterService::ChapterService(ChapterRepository& repo) : repo_(repo) {}

std::optional<Chapter> ChapterService::get(int64_t id) {
    return repo_.find_by_id(id);
}

std::vector<Chapter> ChapterService::list_all() {
    return repo_.find_all();
}

Chapter ChapterService::create(const Chapter& ch) {
    if (ch.name.empty()) {
        throw std::invalid_argument("Chapter name required");
    }
    if (ch.discord_announcement_channel_id.empty()) {
        throw std::invalid_argument("Discord announcement channel ID required");
    }
    return repo_.create(ch);
}

Chapter ChapterService::update(int64_t id, const Chapter& updates) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("Chapter not found: " + std::to_string(id));
    }
    Chapter ch = *existing;
    if (!updates.name.empty()) ch.name = updates.name;
    ch.shorthand = updates.shorthand; // always apply (empty = clear)
    if (!updates.description.empty()) ch.description = updates.description;
    if (!updates.discord_announcement_channel_id.empty()) {
        ch.discord_announcement_channel_id = updates.discord_announcement_channel_id;
    }
    // role fields always applied (empty string = no role)
    ch.discord_lead_role_id   = updates.discord_lead_role_id;
    ch.discord_member_role_id = updates.discord_member_role_id;
    repo_.update(ch);
    return repo_.find_by_id(id).value_or(ch);
}

void ChapterService::delete_chapter(int64_t id) {
    repo_.delete_by_id(id);
}
