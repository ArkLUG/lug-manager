#include "services/EventService.hpp"
#include <openssl/rand.h>
#include <iostream>
#include <stdexcept>
#include <cstdio>

EventService::EventService(EventRepository& repo, DiscordClient& discord,
                            CalendarGenerator& cal,
                            ChapterRepository* chapter_repo, GoogleCalendarClient* gcal)
    : repo_(repo), discord_(discord), cal_(cal), chapter_repo_(chapter_repo), gcal_(gcal) {}

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
    std::string prefix;
    // Status
    if (e.status == "tentative") prefix += "[Tentative] ";
    // Scope
    if (e.scope == "non_lug")        prefix += "[Non-LUG] ";
    else if (e.scope == "lug_wide")  prefix += "[LUG Wide] ";
    // Chapter shorthand
    if (e.chapter_id > 0 && chapter_repo_) {
        auto ch = chapter_repo_->find_by_id(e.chapter_id);
        if (ch && !ch->shorthand.empty()) prefix = "[" + ch->shorthand + "] " + prefix;
    }
    if (!prefix.empty()) copy.title = prefix + copy.title;
    return copy;
}

// Extract city/state from a full address for thread titles.
// "123 Main St, Springfield, IL 62701" → "Springfield, IL"
// "123 Main St, Springfield, IL, United States" → "Springfield, IL"
static std::string shorten_location(const std::string& loc) {
    std::vector<std::string> parts;
    std::istringstream ss(loc);
    std::string part;
    while (std::getline(ss, part, ',')) {
        size_t s = part.find_first_not_of(" \t");
        size_t e = part.find_last_not_of(" \t");
        if (s != std::string::npos) parts.push_back(part.substr(s, e - s + 1));
    }
    if (parts.size() < 3) return loc;

    // Check if the last part looks like a country (all letters, no digits, > 2 chars)
    auto is_country = [](const std::string& s) {
        if (s.size() <= 2) return false; // "IL" is a state, not a country
        for (char c : s) if (std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    };

    size_t last = parts.size() - 1;
    if (is_country(parts[last]) && parts.size() >= 4) last--; // skip country

    std::string state = parts[last];
    std::string city  = parts[last - 1];
    // Strip zip code: "IL 62701" → "IL"
    size_t sp = state.find(' ');
    if (sp != std::string::npos) state = state.substr(0, sp);
    return city + ", " + state;
}

// Format: "{title} | {location} | {dates}"
static std::string format_thread_name(const LugEvent& e) {
    auto fmt_date = [](const std::string& iso) -> std::string {
        if (iso.size() < 10) return iso;
        try {
            int month = std::stoi(iso.substr(5, 2));
            int day   = std::stoi(iso.substr(8, 2));
            std::string year = iso.substr(2, 2); // "2026" → "26"
            if (month >= 1 && month <= 12)
                return std::to_string(month) + "/" + std::to_string(day) + "/" + year;
        } catch (...) {}
        return iso.substr(0, 10);
    };

    std::string d0 = fmt_date(e.start_time);
    std::string d1 = fmt_date(e.end_time);
    std::string date_part = (d0 == d1 || d1.empty()) ? d0 : d0 + "-" + d1;

    std::string name = e.title;
    if (!e.location.empty()) name += " | " + shorten_location(e.location);
    if (!date_part.empty())  name += " | " + date_part;
    if (name.size() > 100)   name = name.substr(0, 100);
    return name;
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
    cal_.invalidate();
    return created;
}

LugEvent EventService::create(const LugEvent& e) {
    LugEvent to_create = e;
    to_create.ical_uid = generate_uuid();

    LugEvent created = repo_.create(to_create);

    if (!created.suppress_discord) {
        try {
            std::string thread_id   = created.discord_thread_id; // pre-set if user chose existing
            std::string thread_name = format_thread_name(created);
            std::string lug_msg_id;

            std::string role = (created.scope == "non_lug")
                ? discord_.get_non_lug_event_role_id()
                : discord_.get_announcement_role_id();

            std::cout << "[EventService] Discord integration for event " << created.id
                      << " '" << created.title << "'\n";
            std::cout << "[EventService]   lug_channel_id='"
                      << discord_.get_lug_channel_id() << "'"
                      << "  forum_channel_id='" << discord_.get_events_forum_channel_id() << "'"
                      << "  role='" << role << "'\n";

            // Step 1: Create forum thread FIRST so we can link to it in the announcement.
            if (thread_id.empty() && !discord_.get_events_forum_channel_id().empty()) {
                thread_id = discord_.sync_create_forum_thread_for_event(thread_name, created);
            }

            // Build thread URL now if we have a thread (forum or pre-existing)
            std::string thread_url;
            if (!thread_id.empty() && !discord_.get_guild_id().empty())
                thread_url = "https://discord.com/channels/" + discord_.get_guild_id() + "/" + thread_id;

            // Step 2: Post announcement to lug_channel with thread link
            if (!discord_.get_lug_channel_id().empty()) {
                lug_msg_id = discord_.sync_post_event_announcement(
                    discord_.get_lug_channel_id(), created, role, thread_url);
                if (lug_msg_id.empty())
                    std::cerr << "[EventService] Warning: lug_channel announcement post returned no message ID\n";
                else
                    std::cout << "[EventService]   lug announcement posted, msg_id=" << lug_msg_id << "\n";
            } else {
                std::cerr << "[EventService] Warning: lug_channel_id is not configured — skipping announcement\n";
            }

            // Step 3: If no forum channel, create a text thread from the announcement message,
            // then edit the announcement to add the thread link.
            if (thread_id.empty() && !lug_msg_id.empty()) {
                thread_id = discord_.sync_create_thread_from_message(
                    discord_.get_lug_channel_id(), lug_msg_id, thread_name);
                if (!thread_id.empty() && !discord_.get_guild_id().empty()) {
                    thread_url = "https://discord.com/channels/" + discord_.get_guild_id() + "/" + thread_id;
                    std::string updated_content =
                        DiscordClient::build_event_announcement_content(created, role, thread_url, discord_.get_suppress_pings());
                    discord_.update_channel_message(discord_.get_lug_channel_id(), lug_msg_id, updated_content);
                }
            }

            // Step 4: Create Discord scheduled event
            std::string event_id = discord_.sync_create_scheduled_event_event(created);

            repo_.update_discord_ids(created.id, thread_id, event_id);
            if (!lug_msg_id.empty()) repo_.update_lug_message_id(created.id, lug_msg_id);

            created.discord_thread_id      = thread_id;
            created.discord_event_id       = event_id;
            created.discord_lug_message_id = lug_msg_id;
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Warning: failed to post Discord integration for event "
                      << created.id << ": " << ex.what() << "\n";
        }

        // Post to chapter announcement channel (if chapter event)
        if (created.chapter_id > 0 && chapter_repo_) {
            try {
                auto ch = chapter_repo_->find_by_id(created.chapter_id);
                if (ch) {
                    std::cout << "[EventService]   chapter_id=" << created.chapter_id
                              << "  chapter_announcement_channel='"
                              << ch->discord_announcement_channel_id << "'\n";
                    if (!ch->discord_announcement_channel_id.empty()) {
                        std::string ch_role = ch->discord_member_role_id.empty()
                            ? discord_.get_announcement_role_id()
                            : ch->discord_member_role_id;
                        std::string ch_thread_url;
                        if (!created.discord_thread_id.empty() && !discord_.get_guild_id().empty())
                            ch_thread_url = "https://discord.com/channels/" + discord_.get_guild_id() + "/" + created.discord_thread_id;
                        std::string msg_id = discord_.sync_post_event_announcement(
                            ch->discord_announcement_channel_id, created, ch_role, ch_thread_url);
                        if (!msg_id.empty()) {
                            repo_.update_chapter_message_id(created.id, msg_id);
                            created.discord_chapter_message_id = msg_id;
                            std::cout << "[EventService]   chapter announcement posted, msg_id=" << msg_id << "\n";
                        } else {
                            std::cerr << "[EventService] Warning: chapter announcement post returned no message ID\n";
                        }
                    } else {
                        std::cerr << "[EventService] Warning: chapter " << created.chapter_id
                                  << " has no discord_announcement_channel_id configured\n";
                    }
                }
            } catch (const std::exception& ex) {
                std::cerr << "[EventService] Warning: failed to post chapter announcement for event "
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
                std::cout << "[EventService]   Google Calendar event created, id=" << gcal_id << "\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Warning: failed to create Google Calendar event: " << ex.what() << "\n";
        }
    }

    cal_.invalidate();
    return created;
}

LugEvent EventService::update(int64_t id, const LugEvent& updates) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("LugEvent not found: " + std::to_string(id));
    }

    LugEvent updated = *existing;
    if (!updates.title.empty())           updated.title           = updates.title;
    if (!updates.description.empty())     updated.description     = updates.description;
    if (!updates.location.empty())        updated.location        = updates.location;
    if (!updates.start_time.empty())      updated.start_time      = updates.start_time;
    if (!updates.end_time.empty())        updated.end_time        = updates.end_time;
    if (!updates.status.empty())          updated.status          = updates.status;
    if (!updates.signup_deadline.empty()) updated.signup_deadline = updates.signup_deadline;
    if (!updates.scope.empty())            updated.scope           = updates.scope;
    if (updates.chapter_id > 0)           updated.chapter_id      = updates.chapter_id;
    else if (updated.scope == "lug_wide" || updated.scope == "non_lug")
                                          updated.chapter_id      = 0;
    if (updates.max_attendees > 0)        updated.max_attendees        = updates.max_attendees;
    if (!updates.discord_thread_id.empty()) updated.discord_thread_id = updates.discord_thread_id;
    if (updates.event_lead_id > 0)        updated.event_lead_id        = updates.event_lead_id;
    else if (updates.event_lead_id == -1) updated.event_lead_id        = 0; // explicit clear
    // Ping roles: always apply (caller sets to "" to clear, or CSV to replace)
    // Only updated when the caller explicitly provides the field (routes handle the guard)
    if (updates.discord_ping_role_ids != "\x01")
        updated.discord_ping_role_ids = updates.discord_ping_role_ids;
    // Suppress flags: always take from updates (route sets explicitly)
    updated.suppress_discord  = updates.suppress_discord;
    updated.suppress_calendar = updates.suppress_calendar;
    updated.notes             = updates.notes;
    updated.entrance_fee      = updates.entrance_fee;
    updated.public_kids       = updates.public_kids;
    updated.public_teens      = updates.public_teens;
    updated.public_adults     = updates.public_adults;
    updated.social_media_links = updates.social_media_links;
    updated.event_feedback    = updates.event_feedback;

    repo_.update(updated);

    if (!updated.suppress_discord) {
        try {
            discord_.update_event(updated);
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Warning: failed to update Discord event for event "
                      << updated.id << ": " << ex.what() << "\n";
        }

        // Build thread URL for inclusion in announcement messages
        std::string thread_url;
        if (!updated.discord_thread_id.empty() && !discord_.get_guild_id().empty())
            thread_url = "https://discord.com/channels/" + discord_.get_guild_id() + "/" + updated.discord_thread_id;

        // Detect scope/chapter change — need to move announcements
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
                        std::string msg_id = discord_.sync_post_event_announcement(
                            discord_.get_lug_channel_id(), updated, role, thread_url);
                        if (!msg_id.empty()) {
                            repo_.update_lug_message_id(updated.id, msg_id);
                            updated.discord_lug_message_id = msg_id;
                        }
                    } catch (const std::exception& ex) {
                        std::cerr << "[EventService] Warning: failed to post lug announcement: " << ex.what() << "\n";
                    }
                }
            }
            if ((updated.scope == "chapter" || scope_changed) && updated.chapter_id > 0 && chapter_repo_) {
                try {
                    auto ch = chapter_repo_->find_by_id(updated.chapter_id);
                    if (ch && !ch->discord_announcement_channel_id.empty()) {
                        std::string ch_role = ch->discord_member_role_id.empty()
                            ? discord_.get_announcement_role_id()
                            : ch->discord_member_role_id;
                        std::string msg_id = discord_.sync_post_event_announcement(
                            ch->discord_announcement_channel_id, updated, ch_role, thread_url);
                        if (!msg_id.empty()) {
                            repo_.update_chapter_message_id(updated.id, msg_id);
                            updated.discord_chapter_message_id = msg_id;
                        }
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "[EventService] Warning: failed to post chapter announcement: " << ex.what() << "\n";
                }
            }
        } else {
            // Same scope — edit announcements in place
            if (!updated.discord_lug_message_id.empty() && !discord_.get_lug_channel_id().empty()) {
                try {
                    std::string role = (updated.scope == "non_lug")
                        ? discord_.get_non_lug_event_role_id()
                        : discord_.get_announcement_role_id();
                    std::string new_content =
                        DiscordClient::build_event_announcement_content(updated, role, thread_url, discord_.get_suppress_pings());
                    discord_.update_channel_message(discord_.get_lug_channel_id(),
                                                    updated.discord_lug_message_id, new_content);
                } catch (const std::exception& ex) {
                    std::cerr << "[EventService] Warning: failed to update lug announcement: " << ex.what() << "\n";
                }
            }
            if (!updated.discord_chapter_message_id.empty() && updated.chapter_id > 0 && chapter_repo_) {
                try {
                    auto ch = chapter_repo_->find_by_id(updated.chapter_id);
                    if (ch && !ch->discord_announcement_channel_id.empty()) {
                        std::string ch_role = ch->discord_member_role_id.empty()
                            ? discord_.get_announcement_role_id()
                            : ch->discord_member_role_id;
                        std::string new_content =
                            DiscordClient::build_event_announcement_content(updated, ch_role, thread_url, discord_.get_suppress_pings());
                        discord_.update_channel_message(ch->discord_announcement_channel_id,
                                                        updated.discord_chapter_message_id, new_content);
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "[EventService] Warning: failed to update chapter announcement: " << ex.what() << "\n";
                }
            }
        }

        // Post update notification in the thread (no pings, can be suppressed)
        if (!discord_.get_suppress_updates() && !updated.discord_thread_id.empty()) {
            try {
                discord_.post_message(updated.discord_thread_id,
                    "**Event Updated** — " + updated.title + " has been updated.");
            } catch (const std::exception& ex) {
                std::cerr << "[EventService] Warning: failed to post update notification: " << ex.what() << "\n";
            }
        }
    } // end suppress_discord check

    // Google Calendar update
    if (!updated.suppress_calendar && gcal_ && gcal_->is_configured() && !updated.google_calendar_event_id.empty()) {
        try {
            gcal_->update_event(updated.google_calendar_event_id, with_calendar_title(updated));
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Warning: failed to update Google Calendar event: " << ex.what() << "\n";
        }
    }

    cal_.invalidate();

    auto refreshed = repo_.find_by_id(id);
    return refreshed.value_or(updated);
}

void EventService::cancel(int64_t id) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("LugEvent not found: " + std::to_string(id));
    }

    // Delete from Discord (fire-and-forget, errors are logged not thrown)
    if (!existing->discord_event_id.empty())
        discord_.delete_scheduled_event(existing->discord_event_id);
    if (!existing->discord_thread_id.empty())
        discord_.delete_channel(existing->discord_thread_id);
    if (!existing->discord_lug_message_id.empty() && !discord_.get_lug_channel_id().empty())
        discord_.delete_channel_message(discord_.get_lug_channel_id(), existing->discord_lug_message_id);
    if (!existing->discord_chapter_message_id.empty() && chapter_repo_) {
        try {
            auto ch = chapter_repo_->find_by_id(existing->chapter_id);
            if (ch && !ch->discord_announcement_channel_id.empty())
                discord_.delete_channel_message(ch->discord_announcement_channel_id,
                                                existing->discord_chapter_message_id);
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Warning: failed to delete chapter announcement for event "
                      << existing->id << ": " << ex.what() << "\n";
        }
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
            update(e.id, e); // triggers Discord scheduled event + announcement updates
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

    LugEvent updated = *existing;
    updated.status = status;
    repo_.update(updated);

    // Sync status to Google Calendar
    if (gcal_ && gcal_->is_configured() && !updated.google_calendar_event_id.empty()) {
        try {
            gcal_->update_event(updated.google_calendar_event_id, with_calendar_title(updated));
        } catch (const std::exception& ex) {
            std::cerr << "[EventService] Warning: failed to update Google Calendar status: " << ex.what() << "\n";
        }
    }

    cal_.invalidate();
}
