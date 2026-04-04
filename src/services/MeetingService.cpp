#include "services/MeetingService.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdio>

MeetingService::MeetingService(MeetingRepository& repo, DiscordClient& discord,
                                CalendarGenerator& cal,
                                ChapterRepository* chapter_repo, GoogleCalendarClient* gcal)
    : repo_(repo), discord_(discord), cal_(cal), chapter_repo_(chapter_repo), gcal_(gcal) {}

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

std::vector<Meeting> MeetingService::list_paginated(const std::string& search, int limit, int offset,
                                                    const std::string& sort_col,
                                                    const std::string& sort_dir) {
    return repo_.find_paginated(search, limit, offset, sort_col, sort_dir);
}
int MeetingService::count_filtered(const std::string& search) { return repo_.count_filtered(search); }
int MeetingService::count_all() { return repo_.count_all(); }

bool MeetingService::exists_by_google_calendar_id(const std::string& gcal_event_id) {
    return repo_.exists_by_google_calendar_id(gcal_event_id);
}

// Build a calendar-friendly title: [NWA] [Non-LUG] Title
Meeting MeetingService::with_calendar_title(const Meeting& m) const {
    Meeting copy = m;
    std::string prefix;
    if (m.scope == "non_lug")        prefix += "[Non-LUG] ";
    else if (m.scope == "lug_wide")  prefix += "[LUG Wide] ";
    if (m.chapter_id > 0 && chapter_repo_) {
        auto ch = chapter_repo_->find_by_id(m.chapter_id);
        if (ch && !ch->shorthand.empty()) prefix = "[" + ch->shorthand + "] " + prefix;
    }
    if (!prefix.empty()) copy.title = prefix + copy.title;
    return copy;
}

std::optional<Meeting> MeetingService::get(int64_t id) {
    return repo_.find_by_id(id);
}

Meeting MeetingService::create_imported(const Meeting& m) {
    Meeting to_create = m;
    to_create.ical_uid = generate_uuid();
    Meeting created = repo_.create(to_create);
    if (!to_create.google_calendar_event_id.empty()) {
        repo_.update_google_calendar_event_id(created.id, to_create.google_calendar_event_id);
        created.google_calendar_event_id = to_create.google_calendar_event_id;
    }
    cal_.invalidate();
    return created;
}

Meeting MeetingService::create(const Meeting& m) {
    Meeting to_create = m;
    to_create.ical_uid = generate_uuid();

    Meeting created = repo_.create(to_create);

    if (!created.suppress_discord) {
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
    } // end suppress_discord check

    // Google Calendar event
    if (!created.suppress_calendar && gcal_ && gcal_->is_configured()) {
        try {
            std::string gcal_id = gcal_->create_event(with_calendar_title(created));
            if (!gcal_id.empty()) {
                repo_.update_google_calendar_event_id(created.id, gcal_id);
                created.google_calendar_event_id = gcal_id;
                std::cout << "[MeetingService]   Google Calendar event created, id=" << gcal_id << "\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to create Google Calendar event: " << ex.what() << "\n";
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
    if (!updates.scope.empty())       updated.scope       = updates.scope;
    if (updates.chapter_id > 0)       updated.chapter_id  = updates.chapter_id;
    else if (updates.scope == "lug_wide" || updates.scope == "non_lug")
                                      updated.chapter_id  = 0; // clear chapter when switching to non-chapter scope
    // Virtual, suppress flags, and notes: always take from updates
    updated.is_virtual                = updates.is_virtual;
    updated.discord_voice_channel_id  = updates.discord_voice_channel_id;
    updated.suppress_discord  = updates.suppress_discord;
    updated.suppress_calendar = updates.suppress_calendar;
    updated.notes             = updates.notes;

    repo_.update(updated);

    if (!updated.suppress_discord) {
        // Update Discord scheduled event
        if (!updated.discord_event_id.empty()) {
            try {
                discord_.update_scheduled_event(updated);
            } catch (const std::exception& ex) {
                std::cerr << "[MeetingService] Warning: failed to update Discord scheduled event for meeting "
                          << updated.id << ": " << ex.what() << "\n";
            }
        }

        // Detect scope change — need to move announcements between channels
        bool scope_changed = existing->scope != updated.scope || existing->chapter_id != updated.chapter_id;

        if (scope_changed) {
            // Delete old announcements
            if (!existing->discord_lug_message_id.empty() && !discord_.get_lug_channel_id().empty())
                discord_.delete_channel_message(discord_.get_lug_channel_id(), existing->discord_lug_message_id);
            if (!existing->discord_chapter_message_id.empty() && existing->chapter_id > 0 && chapter_repo_) {
                auto old_ch = chapter_repo_->find_by_id(existing->chapter_id);
                if (old_ch && !old_ch->discord_announcement_channel_id.empty())
                    discord_.delete_channel_message(old_ch->discord_announcement_channel_id, existing->discord_chapter_message_id);
            }
            updated.discord_lug_message_id.clear();
            updated.discord_chapter_message_id.clear();
            repo_.update_lug_message_id(updated.id, "");
            repo_.update_chapter_message_id(updated.id, "");

            // Post new announcement in the correct channel
            if (updated.scope == "lug_wide" || updated.scope == "non_lug") {
                if (!discord_.get_lug_channel_id().empty()) {
                    try {
                        std::string role = (updated.scope == "non_lug")
                            ? discord_.get_non_lug_event_role_id()
                            : discord_.get_announcement_role_id();
                        std::string msg_id = discord_.sync_post_meeting_announcement(
                            discord_.get_lug_channel_id(), updated, role);
                        if (!msg_id.empty()) {
                            repo_.update_lug_message_id(updated.id, msg_id);
                            updated.discord_lug_message_id = msg_id;
                        }
                    } catch (const std::exception& ex) {
                        std::cerr << "[MeetingService] Warning: failed to post lug announcement: " << ex.what() << "\n";
                    }
                }
            } else if (updated.scope == "chapter" && updated.chapter_id > 0 && chapter_repo_) {
                try {
                    auto ch = chapter_repo_->find_by_id(updated.chapter_id);
                    if (ch && !ch->discord_announcement_channel_id.empty()) {
                        std::string ch_role = ch->discord_member_role_id.empty()
                            ? discord_.get_announcement_role_id()
                            : ch->discord_member_role_id;
                        std::string msg_id = discord_.sync_post_meeting_announcement(
                            ch->discord_announcement_channel_id, updated, ch_role);
                        if (!msg_id.empty()) {
                            repo_.update_chapter_message_id(updated.id, msg_id);
                            updated.discord_chapter_message_id = msg_id;
                        }
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "[MeetingService] Warning: failed to post chapter announcement: " << ex.what() << "\n";
                }
            }
        } else {
            // Same scope — just edit existing announcements in place
            if (!updated.discord_lug_message_id.empty() && !discord_.get_lug_channel_id().empty()) {
                try {
                    std::string role = (updated.scope == "non_lug")
                        ? discord_.get_non_lug_event_role_id()
                        : discord_.get_announcement_role_id();
                    std::string new_content = DiscordClient::build_meeting_announcement_content(
                        updated, role, discord_.get_timezone(), discord_.get_suppress_pings());
                    discord_.update_channel_message(discord_.get_lug_channel_id(),
                                                    updated.discord_lug_message_id, new_content);
                } catch (const std::exception& ex) {
                    std::cerr << "[MeetingService] Warning: failed to update lug announcement: " << ex.what() << "\n";
                }
            }
            if (!updated.discord_chapter_message_id.empty() && updated.chapter_id > 0 && chapter_repo_) {
                try {
                    auto ch = chapter_repo_->find_by_id(updated.chapter_id);
                    if (ch && !ch->discord_announcement_channel_id.empty()) {
                        std::string ch_role = ch->discord_member_role_id.empty()
                            ? discord_.get_announcement_role_id()
                            : ch->discord_member_role_id;
                        std::string new_content = DiscordClient::build_meeting_announcement_content(
                            updated, ch_role, discord_.get_timezone(), discord_.get_suppress_pings());
                        discord_.update_channel_message(ch->discord_announcement_channel_id,
                                                        updated.discord_chapter_message_id, new_content);
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "[MeetingService] Warning: failed to update chapter announcement: " << ex.what() << "\n";
                }
            }
        }
    } // end suppress_discord check

    // Google Calendar update
    if (!updated.suppress_calendar && gcal_ && gcal_->is_configured() && !updated.google_calendar_event_id.empty()) {
        try {
            gcal_->update_event(updated.google_calendar_event_id, with_calendar_title(updated));
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to update Google Calendar event: " << ex.what() << "\n";
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

    // Google Calendar delete
    if (gcal_ && gcal_->is_configured() && !existing->google_calendar_event_id.empty()) {
        try {
            gcal_->delete_event(existing->google_calendar_event_id);
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Warning: failed to delete Google Calendar event: " << ex.what() << "\n";
        }
    }

    // Delete from DB
    repo_.delete_by_id(existing->id);

    cal_.invalidate();
}

MeetingService::SyncResult MeetingService::sync_all_to_google_calendar() {
    SyncResult result;
    if (!gcal_ || !gcal_->is_configured()) return result;

    auto all = repo_.find_all();
    for (auto& m : all) {
        try {
            auto cal_m = with_calendar_title(m);
            if (m.google_calendar_event_id.empty()) {
                std::string gcal_id = gcal_->create_event(cal_m);
                if (!gcal_id.empty()) {
                    repo_.update_google_calendar_event_id(m.id, gcal_id);
                    ++result.created;
                }
            } else {
                gcal_->update_event(m.google_calendar_event_id, cal_m);
                ++result.synced;
            }
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Sync error for meeting " << m.id << ": " << ex.what() << "\n";
            ++result.errors;
        }
    }
    return result;
}

MeetingService::SyncResult MeetingService::sync_all_to_discord() {
    SyncResult result;
    auto all = repo_.find_all();
    for (auto& m : all) {
        try {
            update(m.id, m);
            ++result.synced;
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingService] Discord sync error for meeting " << m.id << ": " << ex.what() << "\n";
            ++result.errors;
        }
    }
    return result;
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
