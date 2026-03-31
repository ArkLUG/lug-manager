#include "services/MeetingService.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdio>

MeetingService::MeetingService(MeetingRepository& repo, DiscordClient& discord,
                                CalendarGenerator& cal, ChapterRepository* chapter_repo)
    : repo_(repo), discord_(discord), cal_(cal), chapter_repo_(chapter_repo) {}

// static
std::string MeetingService::generate_uuid() {
    unsigned char bytes[16];
    RAND_bytes(bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0f) | 0x40; // version 4
    bytes[8] = (bytes[8] & 0x3f) | 0x80; // variant bits
    char buf[37];
    snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0],  bytes[1],  bytes[2],  bytes[3],
        bytes[4],  bytes[5],  bytes[6],  bytes[7],
        bytes[8],  bytes[9],  bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buf);
}

std::vector<Meeting> MeetingService::list_upcoming() {
    return repo_.find_upcoming();
}

std::vector<Meeting> MeetingService::list_all() {
    return repo_.find_all();
}

std::vector<Meeting> MeetingService::list_by_chapter(int64_t chapter_id) {
    return repo_.find_upcoming_by_chapter(chapter_id);
}

std::vector<Meeting> MeetingService::list_paginated(const std::string& search, int limit, int offset) {
    return repo_.find_paginated(search, limit, offset);
}
int MeetingService::count_filtered(const std::string& search) { return repo_.count_filtered(search); }
int MeetingService::count_all() { return repo_.count_all(); }

std::optional<Meeting> MeetingService::get(int64_t id) {
    return repo_.find_by_id(id);
}

Meeting MeetingService::create(const Meeting& m) {
    Meeting to_create = m;
    to_create.ical_uid = generate_uuid();

    Meeting created = repo_.create(to_create);

    // Discord scheduled event
    try {
        std::string discord_event_id = discord_.sync_create_scheduled_event_meeting(created);
        repo_.update_discord_event_id(created.id, discord_event_id);
        created.discord_event_id = discord_event_id;
    } catch (const std::exception& ex) {
        std::cerr << "[MeetingService] Warning: failed to create Discord scheduled event for meeting "
                  << created.id << ": " << ex.what() << "\n";
    }

    // Announcement in lug channel (lug_wide or non_lug scope)
    if (created.scope == "lug_wide" || created.scope == "non_lug") {
        try {
            if (!discord_.get_lug_channel_id().empty()) {
                std::string role = (created.scope == "non_lug")
                    ? discord_.get_non_lug_event_role_id()
                    : discord_.get_announcement_role_id();
                std::string msg_id = discord_.sync_post_meeting_announcement(
                    discord_.get_lug_channel_id(), created, role);
                if (!msg_id.empty()) {
                    repo_.update_lug_message_id(created.id, msg_id);
                    created.discord_lug_message_id = msg_id;
                    std::cout << "[MeetingService]   lug announcement posted, msg_id=" << msg_id << "\n";
                } else {
                    std::cerr << "[MeetingService] Warning: lug announcement returned no message ID\n";
                }
            } else {
                std::cerr << "[MeetingService] Warning: lug_channel_id not configured\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to post lug announcement for meeting "
                      << created.id << ": " << ex.what() << "\n";
        }
    }

    // Announcement in chapter channel (chapter scope)
    if (created.scope == "chapter" && created.chapter_id > 0 && chapter_repo_) {
        try {
            auto ch = chapter_repo_->find_by_id(created.chapter_id);
            if (ch && !ch->discord_announcement_channel_id.empty()) {
                std::string ch_role = ch->discord_member_role_id.empty()
                    ? discord_.get_announcement_role_id()
                    : ch->discord_member_role_id;
                std::string msg_id = discord_.sync_post_meeting_announcement(
                    ch->discord_announcement_channel_id, created, ch_role);
                if (!msg_id.empty()) {
                    repo_.update_chapter_message_id(created.id, msg_id);
                    created.discord_chapter_message_id = msg_id;
                    std::cout << "[MeetingService]   chapter announcement posted, msg_id=" << msg_id << "\n";
                } else {
                    std::cerr << "[MeetingService] Warning: chapter announcement returned no message ID\n";
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to post chapter announcement for meeting "
                      << created.id << ": " << ex.what() << "\n";
        }
    }

    cal_.invalidate();
    return created;
}

Meeting MeetingService::update(int64_t id, const Meeting& updates) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("Meeting not found: " + std::to_string(id));
    }

    Meeting updated = *existing;
    if (!updates.title.empty())       updated.title       = updates.title;
    if (!updates.description.empty()) updated.description = updates.description;
    if (!updates.location.empty())    updated.location    = updates.location;
    if (!updates.start_time.empty())  updated.start_time  = updates.start_time;
    if (!updates.end_time.empty())    updated.end_time    = updates.end_time;
    if (!updates.status.empty())      updated.status      = updates.status;

    repo_.update(updated);

    // Update Discord scheduled event
    if (!updated.discord_event_id.empty()) {
        try {
            discord_.update_scheduled_event(updated);
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to update Discord scheduled event for meeting "
                      << updated.id << ": " << ex.what() << "\n";
        }
    }

    // Edit lug channel announcement if it exists
    if (!updated.discord_lug_message_id.empty() && !discord_.get_lug_channel_id().empty()) {
        try {
            std::string role = (updated.scope == "non_lug")
                ? discord_.get_non_lug_event_role_id()
                : discord_.get_announcement_role_id();
            std::string new_content = DiscordClient::build_meeting_announcement_content(updated, role, discord_.get_timezone());
            discord_.update_channel_message(discord_.get_lug_channel_id(),
                                            updated.discord_lug_message_id, new_content);
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to update lug announcement for meeting "
                      << updated.id << ": " << ex.what() << "\n";
        }
    }

    // Edit chapter channel announcement if it exists
    if (!updated.discord_chapter_message_id.empty() && updated.chapter_id > 0 && chapter_repo_) {
        try {
            auto ch = chapter_repo_->find_by_id(updated.chapter_id);
            if (ch && !ch->discord_announcement_channel_id.empty()) {
                std::string ch_role = ch->discord_member_role_id.empty()
                    ? discord_.get_announcement_role_id()
                    : ch->discord_member_role_id;
                std::string new_content = DiscordClient::build_meeting_announcement_content(updated, ch_role);
                discord_.update_channel_message(ch->discord_announcement_channel_id,
                                                updated.discord_chapter_message_id, new_content);
            }
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to update chapter announcement for meeting "
                      << updated.id << ": " << ex.what() << "\n";
        }
    }

    cal_.invalidate();

    auto refreshed = repo_.find_by_id(id);
    return refreshed.value_or(updated);
}

void MeetingService::cancel(int64_t id) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("Meeting not found: " + std::to_string(id));
    }

    // Delete from Discord
    if (!existing->discord_event_id.empty()) {
        try {
            discord_.delete_scheduled_event(existing->discord_event_id);
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to delete Discord scheduled event for meeting "
                      << existing->id << ": " << ex.what() << "\n";
        }
    }
    if (!existing->discord_lug_message_id.empty() && !discord_.get_lug_channel_id().empty())
        discord_.delete_channel_message(discord_.get_lug_channel_id(),
                                        existing->discord_lug_message_id);
    if (!existing->discord_chapter_message_id.empty() && existing->chapter_id > 0 && chapter_repo_) {
        try {
            auto ch = chapter_repo_->find_by_id(existing->chapter_id);
            if (ch && !ch->discord_announcement_channel_id.empty())
                discord_.delete_channel_message(ch->discord_announcement_channel_id,
                                                existing->discord_chapter_message_id);
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to delete chapter announcement for meeting "
                      << existing->id << ": " << ex.what() << "\n";
        }
    }

    // Delete from DB
    repo_.delete_by_id(existing->id);

    cal_.invalidate();
}

void MeetingService::complete(int64_t id) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("Meeting not found: " + std::to_string(id));
    }

    Meeting completed = *existing;
    completed.status = "completed";
    repo_.update(completed);

    cal_.invalidate();
}
