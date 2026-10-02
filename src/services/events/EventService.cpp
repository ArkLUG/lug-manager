#include "services/events/EventService.hpp"
#include "services/events/TimeCheck.hpp"
#include <openssl/rand.h>
#include <iostream>
#include <stdexcept>
#include <cstdio>

EventService::EventService(EventRepository& repo, DiscordClient& discord,
                            CalendarGenerator& cal,
                            ChapterRepository* chapter_repo, GoogleCalendarClient* gcal,
                            EventDayRepository* event_day_repo)
    : repo_(repo), discord_(discord), cal_(cal), chapter_repo_(chapter_repo), gcal_(gcal),
      event_day_repo_(event_day_repo) {}

// static
std::string EventService::generate_uuid() {
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

std::vector<LugEvent> EventService::list_upcoming() {
    return repo_.find_upcoming();
}

std::vector<LugEvent> EventService::list_all() {
    return repo_.find_all();
}

std::vector<LugEvent> EventService::list_by_chapter(int64_t chapter_id) {
    return repo_.find_upcoming_by_chapter(chapter_id);
}

std::vector<LugEvent> EventService::list_paginated(const std::string& search, int limit, int offset,
                                                    bool upcoming_only,
                                                    const std::string& sort_col,
                                                    const std::string& sort_dir) {
    return repo_.find_paginated(search, limit, offset, upcoming_only, sort_col, sort_dir);
}
int EventService::count_filtered(const std::string& search, bool upcoming_only) { return repo_.count_filtered(search, upcoming_only); }
int EventService::count_all() { return repo_.count_all(); }

std::optional<LugEvent> EventService::get(int64_t id) {
    return repo_.find_by_id(id);
}

// Build a calendar-friendly title: [Tentative] [NWA] [Non-LUG] Title
LugEvent EventService::with_calendar_title(const LugEvent& e) const {
    LugEvent copy = e;
    // Private events still publish to Google Calendar (so the shared calendar
    // shows the LUG is busy) but with every detail redacted to a generic
    // placeholder - this is the single choke point every gcal_->create_event/
    // update_event call goes through, so redacting here covers all of them.
    if (e.is_private) {
        copy.title       = "Private LUG Event";
        copy.description = "";
        copy.location     = "";
        return copy;
    }
    std::string prefix;
    // Status
    if (e.status == "tentative") prefix += "[Tentative] ";
    // Scope
    if (e.scope == "non_lug")        prefix += "[External] ";
    else if (e.scope == "lug_wide")  prefix += "[Group-wide] ";
    // Chapter shorthand
    if (e.chapter_id > 0 && chapter_repo_) {
        auto ch = chapter_repo_->find_by_id(e.chapter_id);
        if (ch && !ch->shorthand.empty()) prefix = "[" + ch->shorthand + "] " + prefix;
    }
    if (!prefix.empty()) copy.title = prefix + copy.title;
    return copy;
}

bool EventService::exists_by_google_calendar_id(const std::string& gcal_event_id) {
    return repo_.exists_by_google_calendar_id(gcal_event_id);
}

LugEvent EventService::create_imported(const LugEvent& e) {
    LugEvent to_create = e;
    to_create.ical_uid = generate_uuid();
    LugEvent created = repo_.create(to_create);
    // google_calendar_event_id isn't in the INSERT, so persist it via UPDATE
    if (!to_create.google_calendar_event_id.empty()) {
        repo_.update_google_calendar_event_id(created.id, to_create.google_calendar_event_id);
        created.google_calendar_event_id = to_create.google_calendar_event_id;
    }
    if (event_day_repo_) {
        event_day_repo_->sync_for_event(created.id, created.start_time, created.end_time);
    }
    cal_.invalidate();
    return created;
}

LugEvent EventService::create(const LugEvent& e) {
    check_start_end(e.start_time, e.end_time);
    LugEvent to_create = e;
    to_create.ical_uid = generate_uuid();

    LugEvent created = repo_.create(to_create);

    if (event_day_repo_) {
        event_day_repo_->sync_for_event(created.id, created.start_time, created.end_time);
    }

    // A thread the user picked (pre-set id) belongs to them, not the app.
    if (!created.discord_thread_id.empty()) repo_.set_thread_owned(created.id, false);
    cal_.invalidate();

    if (chat_) chat_->set_skipped("event", created.id, e.chat_skip);

    run_external([this, created]() mutable {
        auto now = repo_.find_by_id(created.id);
        if (!now) return; // cancelled before we got to it
        if (!created.suppress_discord && chat_) chat_->event_published(*now);

        // Google Calendar event
        if (!created.suppress_calendar && gcal_ && gcal_->is_configured()) {
            try {
                std::string gcal_id = gcal_->create_event(with_calendar_title(created));
                if (!gcal_id.empty()) {
                    repo_.update_google_calendar_event_id(created.id, gcal_id);
                    created.google_calendar_event_id = gcal_id;
                    std::cout << "[EventService]   Google Calendar event created, id=" << gcal_id << "\n";
                }
            } catch (const std::exception& ex) {
                std::cerr << "[EventService] Warning: failed to create Google Calendar event: " << ex.what() << "\n";
            }
        }
        cal_.invalidate();
    });
    return created;
}

LugEvent EventService::update(int64_t id, const LugEvent& updates, bool replace_text_fields,
                              bool notify) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("LugEvent not found: " + std::to_string(id));
    }

    LugEvent updated = *existing;
    if (!updates.title.empty())           updated.title           = updates.title;
    if (replace_text_fields || !updates.description.empty()) updated.description = updates.description;
    if (replace_text_fields || !updates.location.empty())    updated.location    = updates.location;
    if (!updates.start_time.empty())      updated.start_time      = updates.start_time;
    if (!updates.end_time.empty())        updated.end_time        = updates.end_time;
    check_start_end(updated.start_time, updated.end_time);
    if (!updates.status.empty())          updated.status          = updates.status;
    if (!updates.signup_deadline.empty()) updated.signup_deadline = updates.signup_deadline;
    if (!updates.scope.empty())            updated.scope           = updates.scope;
    if (updates.chapter_id > 0)           updated.chapter_id      = updates.chapter_id;
    // Non-chapter scopes never carry a chapter (callers may pass the old id along)
    if (updated.scope == "lug_wide" || updated.scope == "non_lug") updated.chapter_id = 0;
    if (updates.max_attendees > 0)        updated.max_attendees        = updates.max_attendees;
    if (!updates.discord_thread_id.empty() && updates.discord_thread_id != existing->discord_thread_id) {
        updated.discord_thread_id = updates.discord_thread_id;
        // Routes pass a thread here only when the user picked an existing one
        // or the route itself just created one (thread_mode=new); the latter
        // re-marks ownership after this call.
        repo_.set_thread_owned(id, false);
    }
    if (updates.event_lead_id > 0)        updated.event_lead_id        = updates.event_lead_id;
    else if (updates.event_lead_id == -1) updated.event_lead_id        = 0; // explicit clear
    // Ping roles: always apply (caller sets to "" to clear, or CSV to replace)
    // Only updated when the caller explicitly provides the field (routes handle the guard)
    if (updates.discord_ping_role_ids != "\x01")
        updated.discord_ping_role_ids = updates.discord_ping_role_ids;
    // Suppress flags: always take from updates (route sets explicitly)
    updated.suppress_discord  = updates.suppress_discord;
    updated.suppress_calendar = updates.suppress_calendar;
    updated.is_private        = updates.is_private;
    updated.excludes_perks    = updates.excludes_perks;
    updated.notes             = updates.notes;
    updated.entrance_fee      = updates.entrance_fee;
    updated.public_kids       = updates.public_kids;
    updated.public_teens      = updates.public_teens;
    updated.public_adults     = updates.public_adults;
    updated.social_media_links = updates.social_media_links;
    updated.event_feedback    = updates.event_feedback;

    repo_.update(updated);

    if (event_day_repo_) {
        event_day_repo_->sync_for_event(updated.id, updated.start_time, updated.end_time);
    }

    // External systems (Discord, Google Calendar) - see run_external().
    LugEvent before = *existing;
    if (chat_) chat_->set_skipped("event", id, updates.chat_skip);
    run_external([this, before, updated, notify](){
        LugEvent upd = updated;
        // Chat services (Discord, ...): publish, update in place, or take down - see chat::ChatHub.
        if (chat_) {
            auto now = repo_.find_by_id(upd.id);
            if (now) chat_->event_changed(before, *now, notify);
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
                std::cerr << "[EventService] Warning: Google Calendar update failed: " << ex.what() << "\n";
            }
        }
        cal_.invalidate();
    });
    cal_.invalidate();

    auto refreshed = repo_.find_by_id(id);
    return refreshed.value_or(updated);
}

void EventService::cancel(int64_t id) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("LugEvent not found: " + std::to_string(id));
    }

    if (chat_) {
        LugEvent gone = *existing;
        bool owned = repo_.is_thread_owned(id);
        run_external([this, gone, owned]() { chat_->event_removed(gone, owned); });
    }

    // Google Calendar delete
    if (gcal_ && gcal_->is_configured() && !existing->google_calendar_event_id.empty()) {
        try {
            gcal_->delete_event(existing->google_calendar_event_id);
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Warning: failed to delete Google Calendar event: " << ex.what() << "\n";
        }
    }

    // Delete from DB
    repo_.delete_by_id(id);

    cal_.invalidate();
}

EventService::SyncResult EventService::sync_all_to_google_calendar() {
    SyncResult result;
    if (!gcal_ || !gcal_->is_configured()) return result;

    auto all = repo_.find_all();
    for (auto& e : all) {
        try {
            auto cal_ev = with_calendar_title(e);
            if (e.google_calendar_event_id.empty()) {
                // Create new
                std::string gcal_id = gcal_->create_event(cal_ev);
                if (!gcal_id.empty()) {
                    repo_.update_google_calendar_event_id(e.id, gcal_id);
                    ++result.created;
                }
            } else {
                // Update existing
                gcal_->update_event(e.google_calendar_event_id, cal_ev);
                ++result.synced;
            }
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Sync error for event " << e.id << ": " << ex.what() << "\n";
            ++result.errors;
        }
    }
    return result;
}

EventService::SyncResult EventService::sync_all_to_discord() {
    SyncResult result;
    auto all = repo_.find_all();
    for (auto& e : all) {
        try {
            // Re-sync Discord without posting "Event Updated" into every thread.
            update(e.id, e, /*replace_text_fields=*/true, /*notify=*/false);
            ++result.synced;
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Discord sync error for event " << e.id << ": " << ex.what() << "\n";
            ++result.errors;
        }
    }
    return result;
}

void EventService::update_status(int64_t id, const std::string& status) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("LugEvent not found: " + std::to_string(id));
    }
    // Go through update() so Discord (scheduled event + announcements) reflects
    // the new status too - previously only Google Calendar was updated.
    LugEvent updated = *existing;
    updated.status = status;
    update(id, updated, /*replace_text_fields=*/true, /*notify=*/false);
}

