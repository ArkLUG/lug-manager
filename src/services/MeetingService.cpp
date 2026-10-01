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
                                                    const std::string& sort_dir,
                                                    const std::string& when, const std::string& now) {
    return repo_.find_paginated(search, limit, offset, sort_col, sort_dir, when, now);
}
int MeetingService::count_filtered(const std::string& search, const std::string& when, const std::string& now) {
    return repo_.count_filtered(search, when, now);
}
int MeetingService::count_all() { return repo_.count_all(); }

bool MeetingService::exists_by_google_calendar_id(const std::string& gcal_event_id) {
    return repo_.exists_by_google_calendar_id(gcal_event_id);
}

// Build a calendar-friendly title: [NWA] [Non-LUG] Title
Meeting MeetingService::with_calendar_title(const Meeting& m) const {
    Meeting copy = m;
    // Private meetings still publish to Google Calendar (so the shared calendar
    // shows the LUG is busy) but with every detail redacted to a generic
    // placeholder - this is the single choke point every gcal_->create_event/
    // update_event call goes through, so redacting here covers all of them.
    if (m.is_private) {
        copy.title       = "Private LUG Meeting";
        copy.description = "";
        copy.location     = "";
        return copy;
    }
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
    cal_.invalidate();

    if (chat_) chat_->set_skipped("meeting", created.id, m.chat_skip);

    run_external([this, created]() mutable {
        auto now = repo_.find_by_id(created.id);
        if (!now) return; // cancelled before we got to it
        if (!created.suppress_discord && chat_) chat_->meeting_published(*now);

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
    });
    return created;
}

Meeting MeetingService::update(int64_t id, const Meeting& updates, bool replace_text_fields) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("Meeting not found: " + std::to_string(id));
    }

    Meeting updated = *existing;
    if (!updates.title.empty())       updated.title       = updates.title;
    if (replace_text_fields || !updates.description.empty()) updated.description = updates.description;
    if (replace_text_fields || !updates.location.empty())    updated.location    = updates.location;
    if (!updates.start_time.empty())  updated.start_time  = updates.start_time;
    if (!updates.end_time.empty())    updated.end_time    = updates.end_time;
    if (!updates.status.empty())      updated.status      = updates.status;
    if (!updates.scope.empty())       updated.scope       = updates.scope;
    if (updates.chapter_id > 0)       updated.chapter_id  = updates.chapter_id;
    // Non-chapter scopes never carry a chapter (callers may pass the old id along)
    if (updated.scope == "lug_wide" || updated.scope == "non_lug") updated.chapter_id = 0;
    // Virtual, suppress flags, and notes: always take from updates
    updated.is_virtual                = updates.is_virtual;
    updated.discord_voice_channel_id  = updates.discord_voice_channel_id;
    updated.suppress_discord  = updates.suppress_discord;
    updated.suppress_calendar = updates.suppress_calendar;
    updated.is_private        = updates.is_private;
    updated.excludes_perks    = updates.excludes_perks;
    updated.notes             = updates.notes;

    repo_.update(updated);

    // External systems (Discord, Google Calendar) - see run_external().
    Meeting before = *existing;
    if (chat_) chat_->set_skipped("meeting", id, updates.chat_skip);
    run_external([this, before, updated](){
        Meeting upd = updated;
        // Chat services (Discord, ...): publish, update in place, or take down - see chat::ChatHub.
        if (chat_) {
            auto now = repo_.find_by_id(upd.id);
            if (now) chat_->meeting_changed(before, *now);
        }

        // Google Calendar: update, or follow a suppress_calendar toggle
        if (gcal_ && gcal_->is_configured()) {
            try {
                if (upd.suppress_calendar) {
                    if (!upd.google_calendar_event_id.empty()) {
                        gcal_->delete_event(upd.google_calendar_event_id);
                        repo_.update_google_calendar_event_id(upd.id, "");
                    }
                } else if (upd.google_calendar_event_id.empty()) {
                    std::string gcal_id = gcal_->create_event(with_calendar_title(upd));
                    if (!gcal_id.empty()) repo_.update_google_calendar_event_id(upd.id, gcal_id);
                } else {
                    gcal_->update_event(upd.google_calendar_event_id, with_calendar_title(upd));
                }
            } catch (const std::exception& ex) {
                std::cerr << "[MeetingService] Warning: Google Calendar update failed: " << ex.what() << "\n";
            }
        }
        cal_.invalidate();
    });
    cal_.invalidate();

    auto refreshed = repo_.find_by_id(id);
    return refreshed.value_or(updated);
}

void MeetingService::cancel(int64_t id) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("Meeting not found: " + std::to_string(id));
    }

    if (chat_) {
        Meeting gone = *existing;
        run_external([this, gone]() { chat_->meeting_removed(gone); });
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

